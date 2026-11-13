#include "pages.hpp"
#include "gui_forms/host/services/host_services.hpp"

#include "control-spectrum_form.hpp"

namespace gui_forms::gallery::pages {
namespace {

// Owns the control-spectrum page: the controls
// demo/pages/control-spectrum.form describes, and their behaviour. This
// Component owns the handler connections and the page's application state
// (the "sound enabled" Value) for exactly as long as the Gallery lives.
class ControlSpectrumPage final : public Component {
public:
    ControlSpectrumPage(const std::shared_ptr<Surface>& body,
                        const std::shared_ptr<PageContext>& context)
        : status_((*context).status) {
        document_.initialize_component(*this);
        mount_document(body);
        connect_buttons();
        connect_choices();
        connect_cues();
    }

private:
    static constexpr std::array<std::pair<HostSoundCue, std::string_view>, 4> cues{{
        {HostSoundCue::notification, "Notice"},
        {HostSoundCue::success, "Success"},
        {HostSoundCue::warning, "Warning"},
        {HostSoundCue::error, "Error"},
    }};

    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, a design surface of the same 1000 x 660
        // size, where each is placed by its bounds in design coordinates. The
        // copy of the child list keeps the iteration valid while the root
        // shrinks.
        Control& root = *document_.gallery_controls_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
    }

    void connect_buttons() {
        const std::array<std::shared_ptr<Button>, 4> buttons{
            document_.gallery_controls_button_0, document_.gallery_controls_button_1,
            document_.gallery_controls_button_2, document_.gallery_controls_button_3};
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            on((*buttons[index]).clicked(), *this, &ControlSpectrumPage::on_style_button,
               static_cast<int>(index));
        }
        on((*document_.gallery_controls_button_default).clicked(), *this,
           &ControlSpectrumPage::on_default_action);
    }

    void connect_choices() {
        const std::array<std::shared_ptr<CheckBox>, 3> checks{
            document_.gallery_controls_check_0, document_.gallery_controls_check_1,
            document_.gallery_controls_check_2};
        for (std::size_t index = 0; index < checks.size(); ++index) {
            // State event: fires for pointer, keyboard, and programmatic changes alike.
            on((*checks[index]).checked_changed(), *this, &ControlSpectrumPage::on_check_changed,
               static_cast<int>(index));
        }
        on((*document_.gallery_controls_check_tri).check_state_changed(), *this,
           &ControlSpectrumPage::on_tri_state_changed);
        const std::array<std::shared_ptr<RadioButton>, 3> radios{
            document_.gallery_controls_radio_0, document_.gallery_controls_radio_1,
            document_.gallery_controls_radio_2};
        for (std::size_t index = 0; index < radios.size(); ++index) {
            on((*radios[index]).checked_changed(), *this, &ControlSpectrumPage::on_radio_changed,
               static_cast<int>(index));
        }
    }

    void connect_cues() {
        // The CheckBox borrows the page-owned Value; the cue handlers read the
        // Value rather than the control, and the Value's state event reports
        // every change regardless of its origin.
        (*document_.gallery_controls_sound_enabled).bind(sound_enabled_);
        on(sound_enabled_.changed(), *this, &ControlSpectrumPage::on_sound_enabled_changed);
        const std::array<std::shared_ptr<Button>, 4> cue_buttons{
            document_.gallery_controls_sound_0, document_.gallery_controls_sound_1,
            document_.gallery_controls_sound_2, document_.gallery_controls_sound_3};
        for (std::size_t index = 0U; index < cue_buttons.size(); ++index) {
            on((*cue_buttons[index]).clicked(), *this, &ControlSpectrumPage::on_cue_clicked,
               static_cast<int>(index));
        }
    }

    void on_style_button(const int index, ButtonBase& source) {
        static constexpr std::array<std::string_view, 4> names{
            "standard", "flat", "accent", "command"};
        (*status_).text = "Button " + std::string(names[static_cast<std::size_t>(index)]) +
                            " clicked · " + std::string(source.stable_id().value()) +
                            " · pointer, Space, Enter and mnemonic converge on one click";
    }

    void on_default_action() {
        (*status_).text = "Default action clicked · the same validated path serves "
                            "a Window accept button";
    }

    void on_check_changed(const int index, const bool checked) {
        (*status_).text = "Check box " + std::to_string(index + 1) +
                            (checked ? " checked" : " unchecked") +
                            " · checked_changed reports every state change";
    }

    void on_tri_state_changed(const CheckState state) {
        (*status_).text = std::string("Three-state box now ") +
                            (state == CheckState::checked ? "checked"
                             : state == CheckState::indeterminate ? "indeterminate"
                                                                  : "unchecked");
    }

    void on_radio_changed(const int index, const bool checked) {
        if (!checked) return;
        (*status_).text = "Radio " + std::to_string(index + 1) +
                            " checked · its group mates unchecked themselves";
    }

    void on_sound_enabled_changed(const bool enabled) {
        (*status_).text = enabled ? "Sound cues enabled · host playback at 0.72 gain"
                                    : "Sound cues muted · visual meaning remains complete";
    }

    void on_cue_clicked(const int index, ButtonBase& source) {
        const HostSoundCue cue = cues[static_cast<std::size_t>(index)].first;
        HostServices* services = source.attached_window() == nullptr
            ? nullptr : (*source.attached_window()).host_services();
        HostServiceStatus result{HostServiceError::unsupported};
        if (services != nullptr) {
            const std::chrono::steady_clock::duration now =
                std::chrono::steady_clock::now().time_since_epoch();
            const std::chrono::nanoseconds::rep stamp =
                std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            result = (*services).play_sound_cue(
                {cue, sound_enabled_.get() ? 0.72 : 0.0, static_cast<std::uint64_t>(stamp)});
        }
        (*status_).text = std::string("Sound cue ") + host_sound_cue_name(cue) + " · " +
                            host_service_error_name(result.error) +
                            " · visual meaning remains complete when muted";
    }

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    ControlSpectrumDocument<ControlSpectrumPage> document_{};
    std::shared_ptr<Label> status_;
    Value<bool> sound_enabled_{true};
};

} // namespace

void add_control_spectrum(const std::shared_ptr<Surface>& page,
                          const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ControlSpectrumPage>(page, context));
}

} // namespace gui_forms::gallery::pages
