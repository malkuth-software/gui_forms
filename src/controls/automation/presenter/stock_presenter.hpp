#pragma once

// The stock presenter: what a program launched with --mcp shows its user
// (notes:planning/studies/MCP_AUTOMATION_STUDY.md, section 11; rulings 13,
// 19 and 20), drawn by GUI.Forms in its own design language:
// - the in-use sign (MCP round 1: "connected must look significantly
//   different from ordinary"): each attached window declares its outer
//   band (window_band.hpp), 5 px in the theme's accent, following the
//   window's corners as the host reports them; while a model reads or acts
//   a white glint sweeps round it, and it is still at rest (owner's pick of
//   2026-11-24). The window's content is drawn almost grey
//   (Window::set_content_wash, ThemeDefinition::model_wash_saturation), and
//   its title bar names the model ("<title> — Claude Opus 5.5 is using
//   this", Window::set_title_note); both are restored when the model goes.
//   Reduced motion, ambient motion off and an occluded window turn the
//   glint into a tint change (the band's engine limiter); forced high
//   contrast draws no wash;
// - the tag line: a tooltip on the top border naming the model ("Claude
//   Opus 5.5 via Claude Code"), shown briefly when a model connects;
// - the take-back popup of model.directed: a small window owned by the
//   program's first window, whose one button gives control back to the user;
// - the destructive-act question: "Allow <model> to edit files and perform
//   potentially destructive acts?", Allow and Don't Allow; closing it is
//   Don't Allow.
//
// Both popups are the presenter's own windows (presenter_window): the
// automation never attaches, reads or drives one, and the user's input to
// them is never held, so the take-back button always works.
//
// Ownership: the presenter borrows the control (bind_control), which
// borrows the presenter back; the run owns both and destroys the control
// first. Popups are opened through the run's windowing and destroyed with
// the presenter. Thread: the UI thread of the automation's windows.

#include "../control/automation_control.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <memory>
#include <string>
#include <vector>

namespace gui_forms::automation {

class StockPresenter final : public AutomationPresenter {
public:
    StockPresenter();
    ~StockPresenter() override;
    StockPresenter(const StockPresenter&) = delete;
    StockPresenter& operator=(const StockPresenter&) = delete;

    // The control the popups answer to (take back, the question's answer).
    // Null detaches it; the run detaches before destroying the control.
    void bind_control(AutomationControl* control) noexcept;
    // The run's windowing, from the first application window, and that
    // window's stable ID (the popups' owner). Later calls keep the first.
    void set_windowing(const ApplicationWindowHandle& owner, std::string_view owner_id);
    // Appearance review (--mcp-appearance-review, MCP round 1): the windows
    // look as they do without a model, so captures can be compared with the
    // user's view. No band, no title note, no tag line and no user-step
    // tooltip; the destructive-act question and model.directed's take-back
    // popup stay, being safety, not signs. Before the first window attaches.
    void set_unsigned(bool unsigned_sign) noexcept { unsigned_ = unsigned_sign; }

    void window_attached(Window& window) override;
    void window_detached(Window& window) noexcept override;
    void client_changed(const ClientIdentity& client) override;
    void activity(Window& window, AutomationActivity activity) override;
    void calls_finished() noexcept override;
    void pointer_moved(Window& window, Point position, bool left) noexcept override;
    void control_changed(ControlState state, const ClientIdentity& client) override;
    void ask_consent(const ClientIdentity& client) override;
    void user_step_needed(Window& window, const SemanticNode& node, std::string_view what) override;
    void disconnected() noexcept override;
    [[nodiscard]] bool presenter_window(const SurfaceRoot& window) const noexcept override;

    // The popups' buttons and closes (PresenterPanel, PopupClosed).
    void take_back_chosen();
    void consent_chosen(bool allow);
    void popup_closed(bool question) noexcept;
    // Destroys a popup now (DeferredClose; close_popup posts it).
    void close_popup_now(bool question) noexcept;
    // A read's glint has run its moment (GlintEnd).
    void glint_ended(const WindowRef& window) noexcept;

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
    // Shows the title note on `window`, or clears it (the model went).
    void show_title_note(Window& window, bool shown) noexcept;
    void show_tag_line(Window& window, std::chrono::milliseconds duration);
    // Opens a popup window owned by the first window; false when the run
    // has no windowing (a headless test), which the caller answers.
    bool open_popup(bool question, const std::string& heading, const std::string& text);
    // Closes a popup at the next dispatcher turn of an attached window.
    void close_popup(bool question) noexcept;

    // Posted work holds this weakly; reset first in the destructor.
    std::shared_ptr<StockPresenter*> self_{};
    AutomationControl* control_{};
    ApplicationWindowHandle owner_{};
    std::string owner_id_{};
    ClientIdentity client_{};
    bool connected_{};
    bool unsigned_{};
    std::vector<BandWindow> windows_{};
    WindowRef take_back_{};
    ApplicationWindowHandle take_back_handle_{};
    WindowRef question_{};
    ApplicationWindowHandle question_handle_{};
};

} // namespace gui_forms::automation
