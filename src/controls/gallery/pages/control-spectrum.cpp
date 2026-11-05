#include "pages.hpp"

namespace gui_forms::gallery::pages {
namespace {

// One owner for the page: the retained parent owns every control; this
// Component owns the handler connections and the page's application state
// (the "sound enabled" Value) for exactly as long as the Gallery lives.
class ControlSpectrumPage final : public Component {
public:
    ControlSpectrumPage(const std::shared_ptr<Surface>& body,
                        const std::shared_ptr<PageContext>& context)
        : status_((*context).status) {
        (*body).add_at(label("gallery.controls.heading", "CONTROL SPECTRUM", 22.0, 700,
                             shell_blue_dark), {24.0, 18.0, 500.0, 34.0});
        (*body).add_at(label("gallery.controls.lead",
                             "Every public stock control, variant, state, and keyboard path lives here.",
                             12.0, 400, muted), {26.0, 52.0, 760.0, 24.0});
        build_buttons(body);
        build_checks(body);
        build_radios(body);
        build_typography_and_cues(body);
    }

private:
    static constexpr std::array<std::pair<HostSoundCue, std::string_view>, 4> cues{{
        {HostSoundCue::notification, "Notice"},
        {HostSoundCue::success, "Success"},
        {HostSoundCue::warning, "Warning"},
        {HostSoundCue::error, "Error"},
    }};

    void build_buttons(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> button_group = group(
            "gallery.controls.buttons", "Buttons · four public styles", {930.0, 142.0});
        (*body).add_at(button_group, {24.0, 92.0, 930.0, 142.0});
        const std::array<std::pair<ButtonVisualStyle, std::string_view>, 4> buttons{{
            {ButtonVisualStyle::standard, "&Standard"},
            {ButtonVisualStyle::flat, "&Flat"},
            {ButtonVisualStyle::accent, "&Accent"},
            {ButtonVisualStyle::command, "&Command"},
        }};
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            std::shared_ptr<Button> button = make_control<Button>(
                StableId("gallery.controls.button." + std::to_string(index)),
                std::string(buttons[index].second));
            (*button).set_visual_style(buttons[index].first);
            (*button).set_style(page_style());
            (*button_group).add_at(button,
                                   {24.0 + static_cast<double>(index) * 215.0, 42.0, 185.0, 42.0});
            on((*button).clicked(), *this, &ControlSpectrumPage::on_style_button,
               static_cast<int>(index));
        }
        std::shared_ptr<Button> disabled = make_control<Button>(
            StableId("gallery.controls.button.disabled"), "Disabled state");
        (*disabled).enabled = false;
        (*button_group).add_at(disabled, {24.0, 92.0, 185.0, 30.0});
        std::shared_ptr<Button> default_button = make_control<Button>(
            StableId("gallery.controls.button.default"), "&Default action");
        (*button_group).add_at(default_button, {239.0, 92.0, 185.0, 30.0});
        on((*default_button).clicked(), *this, &ControlSpectrumPage::on_default_action);
        std::shared_ptr<LinkLabel> link = make_control<LinkLabel>(
            StableId("gallery.controls.link"), "LinkLabel · visited on activation");
        (*button_group).add_at(link, {454.0, 92.0, 260.0, 30.0});
    }

    void build_checks(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> check_group = group(
            "gallery.controls.checks", "Check boxes · classic, modern, toggle", {450.0, 210.0});
        (*body).add_at(check_group, {24.0, 252.0, 450.0, 210.0});
        for (std::size_t index = 0; index < 3U; ++index) {
            std::shared_ptr<CheckBox> check = make_control<CheckBox>(
                StableId("gallery.controls.check." + std::to_string(index)),
                index == 0U ? "&Classic checkbox" : index == 1U
                    ? "&Modern checkbox" : "&Toggle checkbox");
            (*check).set_indicator_style(static_cast<ChoiceIndicatorStyle>(index));
            (*check).set_checked(index != 0U);
            (*check_group).add_at(check,
                                  {24.0, 42.0 + static_cast<double>(index) * 42.0, 300.0, 28.0});
            // State event: fires for pointer, keyboard, and programmatic changes alike.
            on((*check).checked_changed(), *this, &ControlSpectrumPage::on_check_changed,
               static_cast<int>(index));
        }
        std::shared_ptr<CheckBox> tri = make_control<CheckBox>(
            StableId("gallery.controls.check.tri"), "Three-state indeterminate");
        (*tri).set_three_state(true);
        (*tri).set_check_state(CheckState::indeterminate);
        (*check_group).add_at(tri, {24.0, 168.0, 300.0, 28.0});
        on((*tri).check_state_changed(), *this, &ControlSpectrumPage::on_tri_state_changed);
    }

    void build_radios(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> radio_group = group(
            "gallery.controls.radios", "Radio buttons · three indicator systems", {460.0, 210.0});
        (*body).add_at(radio_group, {494.0, 252.0, 460.0, 210.0});
        for (std::size_t index = 0; index < 3U; ++index) {
            std::shared_ptr<RadioButton> radio = make_control<RadioButton>(
                StableId("gallery.controls.radio." + std::to_string(index)),
                index == 0U ? "Classic radio" : index == 1U ? "Modern radio" : "Toggle radio");
            (*radio).set_indicator_style(static_cast<ChoiceIndicatorStyle>(index));
            (*radio).set_group_name("gallery-radio-style");
            (*radio).set_checked(index == 1U);
            (*radio_group).add_at(radio,
                                  {24.0, 42.0 + static_cast<double>(index) * 42.0, 310.0, 28.0});
            on((*radio).checked_changed(), *this, &ControlSpectrumPage::on_radio_changed,
               static_cast<int>(index));
        }
    }

    void build_typography_and_cues(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> typography = group(
            "gallery.controls.typography", "Labels and typography roles", {930.0, 150.0});
        (*body).add_at(typography, {24.0, 480.0, 930.0, 150.0});
        std::shared_ptr<Label> title = label("gallery.controls.type.title",
                                             "Portsmouth Rapids control title", 19.0, 700,
                                             shell_blue_dark);
        (*title).set_font({FontRole::control, 19.0, 700, false});
        (*typography).add_at(title, {24.0, 38.0, 420.0, 30.0});
        std::shared_ptr<Label> content = label(
            "gallery.controls.type.content",
            "Carlito body specimen: editable and field text stays quiet, clear, and host-independent.",
            12.0, 400, ink);
        (*content).set_text_wrapping(TextWrapping::word);
        (*content).set_vertical_alignment(VerticalAlignment::near);
        (*typography).add_at(content, {24.0, 76.0, 430.0, 48.0});
        std::shared_ptr<Label> center = label("gallery.controls.type.center", "Centered label",
                                              12.0, 600, accent);
        (*center).set_alignment(HorizontalAlignment::center);
        (*typography).add_at(center, {24.0, 118.0, 250.0, 20.0});

        (*typography).add_at(label("gallery.controls.sound.title",
                                   "SEMANTIC SOUND CUES · OPTIONAL HOST", 11.0, 700,
                                   shell_blue_dark), {500.0, 34.0, 390.0, 22.0});
        // The CheckBox borrows the page-owned Value; the cue handlers read the
        // Value rather than the control, and the Value's state event reports
        // every change regardless of its origin.
        std::shared_ptr<CheckBox> sound_enabled = make_control<CheckBox>(
            StableId("gallery.controls.sound.enabled"), "Sound enabled");
        (*sound_enabled).set_indicator_style(ChoiceIndicatorStyle::modern);
        (*sound_enabled).bind(sound_enabled_);
        (*typography).add_at(sound_enabled, {500.0, 108.0, 160.0, 26.0});
        on(sound_enabled_.changed(), *this, &ControlSpectrumPage::on_sound_enabled_changed);
        for (std::size_t index = 0U; index < cues.size(); ++index) {
            std::shared_ptr<Button> cue_button = make_control<Button>(
                StableId("gallery.controls.sound." + std::to_string(index)),
                std::string(cues[index].second));
            (*cue_button).set_visual_style(index == 3U ? ButtonVisualStyle::standard
                                                       : ButtonVisualStyle::command);
            (*typography).add_at(cue_button,
                                 {500.0 + static_cast<double>(index) * 98.0, 64.0, 88.0, 32.0});
            on((*cue_button).clicked(), *this, &ControlSpectrumPage::on_cue_clicked,
               static_cast<int>(index));
        }
    }

    void on_style_button(const int index, ButtonBase& source) {
        static constexpr std::array<std::string_view, 4> names{
            "standard", "flat", "accent", "command"};
        (*status_).set_text("Button " + std::string(names[static_cast<std::size_t>(index)]) +
                            " clicked · " + std::string(source.stable_id().value()) +
                            " · pointer, Space, Enter and mnemonic converge on one click");
    }

    void on_default_action() {
        (*status_).set_text("Default action clicked · the same validated path serves "
                            "a Window accept button");
    }

    void on_check_changed(const int index, const bool checked) {
        (*status_).set_text("Check box " + std::to_string(index + 1) +
                            (checked ? " checked" : " unchecked") +
                            " · checked_changed reports every state change");
    }

    void on_tri_state_changed(const CheckState state) {
        (*status_).set_text(std::string("Three-state box now ") +
                            (state == CheckState::checked ? "checked"
                             : state == CheckState::indeterminate ? "indeterminate"
                                                                  : "unchecked"));
    }

    void on_radio_changed(const int index, const bool checked) {
        if (!checked) return;
        (*status_).set_text("Radio " + std::to_string(index + 1) +
                            " checked · its group mates unchecked themselves");
    }

    void on_sound_enabled_changed(const bool enabled) {
        (*status_).set_text(enabled ? "Sound cues enabled · host playback at 0.72 gain"
                                    : "Sound cues muted · visual meaning remains complete");
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
        (*status_).set_text(std::string("Sound cue ") + host_sound_cue_name(cue) + " · " +
                            host_service_error_name(result.error) +
                            " · visual meaning remains complete when muted");
    }

    std::shared_ptr<Label> status_;
    Value<bool> sound_enabled_{true};
};

} // namespace

void add_control_spectrum(const std::shared_ptr<Surface>& page,
                          const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ControlSpectrumPage>(page, context));
}

} // namespace gui_forms::gallery::pages
