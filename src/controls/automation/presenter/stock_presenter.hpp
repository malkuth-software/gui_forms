#pragma once

// The stock presenter: what a program shows its user while a model uses it
// (notes:planning/studies/MCP_AUTOMATION_STUDY.md, section 11; rulings 13,
// 19 and 20; the owner's control model of 2026-11-24), drawn by GUI.Forms
// in its own design language:
// - while a model is connected: each attached window declares its outer
//   band (window_band.hpp), 5 px in the theme's accent, following the
//   window's corners as the host reports them; while a model reads or acts
//   a white glint sweeps round it, and it is still at rest. The title bar
//   names the model ("<title> — Claude Opus 5.5 is connected",
//   Window::set_title_note). Reduced motion, ambient motion off and an
//   occluded window turn the glint into a tint change;
// - AI mode, while the model holds control: grey barber-pole stripes over
//   every control surface (Window::set_model_veil; content such as a canvas
//   stays clear), the X cursor over the window, and the title note "<title>
//   — Claude Opus 5.5 is in control";
// - the tag line: a tooltip on the top border naming the model ("Claude
//   Opus 5.5 via Claude Code"), shown briefly when a model connects;
// - the take question: "Allow <model> to take control?", counting down 10
//   seconds and then allowing; Allow, Don't Allow; closing it is Don't
//   Allow;
// - the take-back dialog, offered by any press in a held window: "Do you
//   want to take back control of this program?" with the warning "<model>
//   has not yet released the application."; Take Back Control ends the
//   model's control, Keep Waiting leaves it;
// - the destructive-act question: "Allow <model> to edit files and perform
//   potentially destructive acts?", Allow and Don't Allow; closing it is
//   Don't Allow.
//
// The dialogs are the presenter's own windows (presenter_window): the
// automation never attaches, reads or drives one, and the user's input to
// them is never held.
//
// Ownership: the presenter borrows the control (bind_control), which
// borrows the presenter back; the run owns both and destroys the control
// first. Dialogs are opened through the run's windowing and destroyed with
// the presenter. Thread: the UI thread of the automation's windows.

#include "../control/automation_control.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace gui_forms {
class Label;
} // namespace gui_forms

namespace gui_forms::automation {

// The presenter's dialogs.
enum class PresenterDialog : std::uint8_t {
    take,
    take_back,
    consent,
};
inline constexpr std::size_t presenter_dialog_count = 3U;

class StockPresenter final : public AutomationPresenter {
public:
    StockPresenter();
    ~StockPresenter() override;

    StockPresenter(const StockPresenter&) = delete;
    StockPresenter& operator=(const StockPresenter&) = delete;

    // The control the dialogs answer to. Null detaches it; the run detaches
    // before destroying the control.
    void bind_control(AutomationControl* control) noexcept;
    // The run's windowing, from the first application window, and that
    // window's stable ID (the dialogs' owner). Later calls keep the first.
    void set_windowing(const ApplicationWindowHandle& owner, std::string_view owner_id);
    // Appearance review (--mcp-appearance-review, MCP round 1): the windows
    // look as they do without a model, so captures can be compared with the
    // user's view. No band, no title note, no veil, no tag line and no
    // user-step tooltip; the destructive-act question stays, being safety.
    // Before the first window attaches.
    void set_unsigned(bool unsigned_sign) noexcept { unsigned_ = unsigned_sign; }

    void window_attached(Window& window) override;
    void window_detached(Window& window) noexcept override;
    void client_changed(const ClientIdentity& client) override;
    void activity(Window& window, AutomationActivity activity) override;
    void calls_finished() noexcept override;
    void pointer_moved(Window& window, Point position, bool left) noexcept override;
    void control_changed(ControlState state, const ClientIdentity& client) override;
    void ask_take(const ClientIdentity& client, std::chrono::milliseconds countdown) override;
    void offer_take_back(const ClientIdentity& client) override;
    void ask_consent(const ClientIdentity& client) override;
    void user_step_needed(Window& window, const SemanticNode& node, std::string_view what) override;
    void disconnected() noexcept override;
    [[nodiscard]] bool presenter_window(const SurfaceRoot& window) const noexcept override;

    // A dialog's buttons and closes (PresenterPanel, DialogClosed).
    // `accepted` is Allow or Take Back Control.
    void dialog_chosen(PresenterDialog dialog, bool accepted);
    void dialog_closed(PresenterDialog dialog) noexcept;
    // Destroys a dialog now (DeferredClose; close_dialog posts it).
    void close_dialog_now(PresenterDialog dialog) noexcept;
    // A read's glint has run its moment (GlintEnd).
    void glint_ended(const WindowRef& window) noexcept;
    // The take question's countdown ticks a second (CountdownTick).
    void countdown_ticked() noexcept;
    // Whether a dialog is open, for tests.
    [[nodiscard]] bool dialog_open(PresenterDialog dialog) const noexcept;

    struct BandWindow final {
        WindowRef window{};
        bool steady{};
        FrameRequestToken glint_end{};
        // The pointer is over the top border (the tag line shows on entry).
        bool over_border{};
    };

private:
    BandWindow* find_band(const Window& window) noexcept;
    void rest(BandWindow& entry) noexcept;
    // The band's look for `window` at rest or in motion, from its theme's
    // accent, or the plain band while no model is connected.
    [[nodiscard]] WindowBandAppearance sign_appearance(const Window& window, bool moving) const;
    // Shows the title note and AI mode on `window` as the connection and
    // control now stand.
    void show_sign(Window& window) noexcept;
    void show_tag_line(Window& window, std::chrono::milliseconds duration);
    // Opens a dialog owned by the first window; false when the run has no
    // windowing (a headless test) or the window could not open, with why
    // into `why`.
    bool open_dialog(PresenterDialog dialog, const std::string& heading, const std::string& text,
                     const std::string& warning, std::string& why);
    // Closes a dialog at the next dispatcher turn of an attached window.
    void close_dialog(PresenterDialog dialog) noexcept;
    // The take question's text for `seconds` left.
    [[nodiscard]] std::string take_text(std::int64_t seconds) const;
    void arm_countdown() noexcept;

    // Posted work holds this weakly; reset first in the destructor.
    std::shared_ptr<StockPresenter*> self_{};
    AutomationControl* control_{};
    ApplicationWindowHandle owner_{};
    std::string owner_id_{};
    ClientIdentity client_{};
    bool connected_{};
    bool in_control_{};
    bool unsigned_{};
    std::vector<BandWindow> windows_{};
    std::array<WindowRef, presenter_dialog_count> dialogs_{};
    std::array<ApplicationWindowHandle, presenter_dialog_count> dialog_handles_{};
    // The take question's countdown: when it ends, and its tick.
    FrameTime countdown_end_{};
    FrameRequestToken countdown_tick_{};
    std::weak_ptr<Label> countdown_label_{};
};

} // namespace gui_forms::automation
