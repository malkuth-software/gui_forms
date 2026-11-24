#include "stock_presenter.hpp"

#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/surface_root/requests/surface_requests.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/window/band/window_band.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace gui_forms::automation {
namespace {

// The band the presenter declares on a window that has none: wide enough
// to read as "a model is using this" from across the room (MCP round 1),
// not the study's thin effect.
constexpr double sign_band_width = 5.0;
// How long the band moves after a read, and after the last call of a run of
// acts ends (a sweep's lap and more); and how long the tag line shows on
// connect.
constexpr std::chrono::milliseconds glint_moment{1800};
constexpr std::chrono::milliseconds tag_line_moment{4000};
// The glint's laps a second (the band's limiter allows at most 1).
constexpr double sweep_frequency = 0.6;
// How long the tag line stays after the pointer enters the top border, and
// how deep the hover zone is at least (study 11.2: it shows on hover).
constexpr std::chrono::milliseconds tag_line_hover{6000};
constexpr double tag_line_hover_zone = 4.0;
constexpr Color white{255U, 255U, 255U, 255U};

// The model as the title note names it: its own name when the client gives
// one, else the client's, else "A model".
std::string model_label(const ClientIdentity& client) {
    if (!client.model_name.empty()) return client.model_name;
    if (!client.client_name.empty()) return client.client_name;
    return std::string("A model");
}

constexpr Size dialog_size{480.0, 200.0};
// The take question's countdown ticks once a second.
constexpr std::chrono::milliseconds countdown_tick{1000};

// A dialog's stable ID and window title.
const char* dialog_id(PresenterDialog dialog) noexcept {
    switch (dialog) {
    case PresenterDialog::take: return "automation.take";
    case PresenterDialog::take_back: return "automation.take-back";
    case PresenterDialog::consent: return "automation.question";
    }
    return "automation.question";
}

const char* dialog_title(PresenterDialog dialog) noexcept {
    switch (dialog) {
    case PresenterDialog::take: return "Allow a model to take control?";
    case PresenterDialog::take_back: return "Take back control?";
    case PresenterDialog::consent: return "Allow destructive acts?";
    }
    return "Allow destructive acts?";
}

std::size_t dialog_index(PresenterDialog dialog) noexcept {
    const std::size_t index = static_cast<std::size_t>(dialog);
    return index < presenter_dialog_count ? index : presenter_dialog_count - 1U;
}

// A dialog's content: the heading, the text, an optional warning, and its
// two buttons. Its buttons' clicks reach the presenter, which outlives the
// dialog (it destroys its dialogs first).
class PresenterPanel final : public Control {
public:
    static constexpr bool initialize_tree_after_construction = true;

    PresenterPanel(StableId stable_id, StockPresenter* presenter, PresenterDialog dialog, std::string heading,
                   std::string text, std::string warning)
        : Control(std::move(stable_id)), presenter_(presenter), dialog_(dialog), heading_(std::move(heading)),
          text_(std::move(text)), warning_(std::move(warning)) {}

    void initialize_control_tree() {
        const Size size = dialog_size;
        const std::string base(stable_id().value());
        const std::shared_ptr<Label> heading = make_control<Label>(StableId(base + ".heading"), heading_);
        (*heading).set_bounds({20.0, 14.0, size.width - 40.0, 24.0});
        add_child(heading);
        const double text_height = warning_.empty() ? size.height - 98.0 : size.height - 126.0;
        text_label_ = make_control<Label>(StableId(base + ".text"), text_);
        (*text_label_).text_wrapping = TextWrapping::word;
        (*text_label_).set_bounds({20.0, 42.0, size.width - 40.0, text_height});
        add_child(text_label_);
        if (!warning_.empty()) {
            const std::shared_ptr<Label> warning = make_control<Label>(StableId(base + ".warning"), warning_);
            (*warning).text_wrapping = TextWrapping::word;
            (*warning).set_bounds({20.0, 42.0 + text_height, size.width - 40.0, 26.0});
            add_child(warning);
        }
        const bool take_back = dialog_ == PresenterDialog::take_back;
        const std::shared_ptr<Button> decline =
            make_control<Button>(StableId(base + ".decline"), take_back ? "Keep Waiting" : "Don't Allow");
        (*decline).set_bounds({size.width - 330.0, size.height - 48.0, 140.0, 32.0});
        on((*decline).clicked(), *this, &PresenterPanel::decline_clicked);
        add_child(decline);
        const std::shared_ptr<Button> accept =
            make_control<Button>(StableId(base + ".accept"), take_back ? "Take Back Control" : "Allow");
        (*accept).set_bounds({size.width - 180.0, size.height - 48.0, 160.0, 32.0});
        on((*accept).clicked(), *this, &PresenterPanel::accept_clicked);
        add_child(accept);
    }

    void accept_clicked(ButtonBase&) {
        if (presenter_ != nullptr) (*presenter_).dialog_chosen(dialog_, true);
    }
    void decline_clicked(ButtonBase&) {
        if (presenter_ != nullptr) (*presenter_).dialog_chosen(dialog_, false);
    }
    [[nodiscard]] const std::shared_ptr<Label>& text_label() const noexcept { return text_label_; }

private:
    StockPresenter* presenter_{};
    PresenterDialog dialog_{PresenterDialog::consent};
    std::string heading_{};
    std::string text_{};
    std::string warning_{};
    std::shared_ptr<Label> text_label_{};
};

// A dialog's closed callback (ApplicationWindowOptions::closed).
struct DialogClosed final {
    StockPresenter* presenter{};
    PresenterDialog dialog{};
    void operator()() const noexcept { (*presenter).dialog_closed(dialog); }
};

// A dialog closes at its own dispatcher turn: never from inside one of its
// own button's clicks, which run on the dialog's window.
struct DeferredClose final {
    std::weak_ptr<StockPresenter*> presenter{};
    PresenterDialog dialog{};
    void operator()() const noexcept {
        const std::shared_ptr<StockPresenter*> live = presenter.lock();
        if (live) (**live).close_dialog_now(dialog);
    }
};

// The take question's countdown tick (schedule_ui_timer's callback).
struct CountdownTick final {
    std::weak_ptr<StockPresenter*> presenter{};
    void operator()(FrameTime) const noexcept {
        const std::shared_ptr<StockPresenter*> live = presenter.lock();
        if (live) (**live).countdown_ticked();
    }
};

// A read's glint ends (schedule_ui_timer's callback).
struct GlintEnd final {
    StockPresenter* presenter{};
    WindowRef window{};
    void operator()(FrameTime) const noexcept { (*presenter).glint_ended(window); }
};

} // namespace

StockPresenter::StockPresenter() : self_(gui_forms::detail::make_library_shared<StockPresenter*>(this)) {}

StockPresenter::~StockPresenter() {
    self_.reset();
    countdown_tick_.disconnect();
    close_dialog_now(PresenterDialog::take);
    close_dialog_now(PresenterDialog::take_back);
    close_dialog_now(PresenterDialog::consent);
    disconnected();
}

void StockPresenter::bind_control(AutomationControl* control) noexcept {
    control_ = control;
}

void StockPresenter::set_windowing(const ApplicationWindowHandle& owner, std::string_view owner_id) {
    if (owner_.active()) return;
    owner_ = owner;
    owner_id_ = std::string(owner_id);
}

StockPresenter::BandWindow* StockPresenter::find_band(const Window& window) noexcept {
    for (BandWindow& entry : windows_) {
        if (entry.window.get() == &window) return &entry;
    }
    return nullptr;
}

void StockPresenter::window_attached(Window& window) {
    // Unsigned: the window is never entered, so no band, note or tag line
    // reaches it.
    if (unsigned_ || find_band(window) != nullptr) return;
    const Size client = window.client_size();
    const Rect content = window.content_rect();
    // A window that reserves a band of its own keeps it; the presenter only
    // changes its appearance.
    const bool band_reserved = content.width < client.width || content.height < client.height;
    if (!band_reserved) {
        WindowBandOptions options{};
        options.width = sign_band_width;
        window.declare_band(options);
    }
    BandWindow entry{};
    entry.window = window.window_ref();
    windows_.push_back(std::move(entry));
    window.set_band_appearance(sign_appearance(window, false));
    show_sign(window);
    if (connected_) show_tag_line(window, tag_line_moment);
}

WindowBandAppearance StockPresenter::sign_appearance(const Window& window, bool moving) const {
    WindowBandAppearance appearance{};
    // No model: the band reserves its width and draws nothing.
    if (!connected_) return appearance;
    const Control::Ptr root = window.root();
    const Color accent = root ? (*root).effective_theme().basic_style().accent : Color{38U, 114U, 185U, 255U};
    // The accent, solid; in motion a white glint runs round it (the band
    // turns the glint into a tint change where motion is stilled).
    appearance.state = moving ? WindowBandState::sweeping : WindowBandState::rest;
    appearance.rest_colour = stxlib::Optional<Color>::of(accent);
    appearance.lit_colour = stxlib::Optional<Color>::of(white);
    appearance.frequency = sweep_frequency;
    return appearance;
}

void StockPresenter::show_sign(Window& window) noexcept {
    const bool connected = connected_ && !unsigned_;
    const bool in_control = connected && in_control_;
    try {
        // AI mode: the stripes over the control surfaces and the X cursor.
        window.set_model_veil(in_control);
        // Windows 11's own border joins the sign in the accent.
        stxlib::Optional<Color> border{};
        const Control::Ptr root = window.root();
        if (connected && root) border = stxlib::Optional<Color>::of((*root).effective_theme().basic_style().accent);
        window.set_system_border_colour(border);
        if (!connected) {
            window.set_title_note(nullptr);
            return;
        }
        const std::string note =
            " \u2014 " + model_label(client_) + (in_control ? " is in control" : " is connected");
        window.set_title_note(note.c_str());
    } catch (...) {
        // A name that is not valid title text, or no memory: the band still
        // shows the model is connected.
    }
}

void StockPresenter::window_detached(Window& window) noexcept {
    for (std::size_t index = 0U; index < windows_.size(); ++index) {
        if (windows_[index].window.get() != &window) continue;
        windows_[index].glint_end.disconnect();
        const bool connected = connected_;
        connected_ = false;
        show_sign(window);
        connected_ = connected;
        windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

namespace {

// The height of `window`'s top border: its band's top edge, or the
// presenter's band width when the window reserves none there.
double top_border_height(const Window& window) noexcept {
    const WindowBandSnapshot band = window.band_snapshot();
    const double top = band.reserved ? band.edges.top : 0.0;
    return top > 0.0 ? top : tag_line_hover_zone;
}

} // namespace

void StockPresenter::show_tag_line(Window& window, std::chrono::milliseconds duration) {
    const Control::Ptr root = window.root();
    if (!root) return;
    ToolTipRequest request{};
    request.text = client_label(client_);
    request.description = "The model connected to this program";
    const Size client = window.client_size();
    request.anchor = Rect{0.0, 0.0, client.width, top_border_height(window)};
    request.duration = stxlib::Optional<std::chrono::milliseconds>::of(duration);
    try {
        static_cast<void>(window.request_tool_tip(root, std::move(request)));
    } catch (const std::invalid_argument&) {
        // A root not yet attached: the next connect shows it.
    }
}

void StockPresenter::client_changed(const ClientIdentity& client) {
    client_ = client;
    connected_ = true;
    for (BandWindow& entry : windows_) {
        Window* const window = entry.window.get();
        if (window == nullptr) continue;
        (*window).set_band_appearance(sign_appearance(*window, entry.steady));
        show_sign(*window);
        if ((*window).active()) show_tag_line(*window, tag_line_moment);
    }
}

void StockPresenter::activity(Window& window, AutomationActivity activity) {
    BandWindow* const entry = find_band(window);
    if (entry == nullptr) return;
    (*entry).glint_end.disconnect();
    switch (activity) {
    case AutomationActivity::reading: {
        // A read moves the band for a moment; while acts run it moves
        // already.
        if ((*entry).steady) return;
        window.set_band_appearance(sign_appearance(window, true));
        const Control::Ptr root = window.root();
        if (root) {
            (*entry).glint_end = window.schedule_ui_timer(*root, glint_moment, FrameClock::now() + glint_moment,
                                                          GlintEnd{this, window.window_ref()});
        }
        return;
    }
    case AutomationActivity::acting:
    case AutomationActivity::waiting:
        // The band moves until the calls finish and a moment after: its
        // motion is ambient, so the act still reaches idle
        // (SurfaceRoot::next_settling_wake).
        (*entry).steady = true;
        window.set_band_appearance(sign_appearance(window, true));
        return;
    }
    rest(*entry);
}

void StockPresenter::rest(BandWindow& entry) noexcept {
    entry.glint_end.disconnect();
    entry.steady = false;
    Window* const window = entry.window.get();
    if (window == nullptr) return;
    try {
        (*window).set_band_appearance(sign_appearance(*window, false));
    } catch (...) {
        // The band refused (a window closing): nothing is left to show.
    }
}

void StockPresenter::glint_ended(const WindowRef& window) noexcept {
    for (BandWindow& entry : windows_) {
        if (entry.window.get() == window.get() && !entry.steady) rest(entry);
    }
}

void StockPresenter::pointer_moved(Window& window, Point position, bool left) noexcept {
    BandWindow* const entry = find_band(window);
    if (entry == nullptr) return;
    const Size client = window.client_size();
    // The hover zone is the top border, widened to a few pixels so a thin
    // band can be found with the pointer.
    const double zone = std::max(top_border_height(window), tag_line_hover_zone);
    const bool over = connected_ && !left && position.y >= 0.0 && position.y < zone && position.x >= 0.0 &&
                      position.x < client.width;
    if (over && !(*entry).over_border) {
        try {
            show_tag_line(window, tag_line_hover);
        } catch (...) {
            // The tool tip could not be shown (out of memory): the next
            // entry tries again.
        }
    }
    (*entry).over_border = over;
}

void StockPresenter::calls_finished() noexcept {
    // The motion runs on a moment after the last call, so a quick act still
    // shows it; a new call before then keeps it moving.
    for (BandWindow& entry : windows_) {
        if (!entry.steady) continue;
        entry.steady = false;
        Window* const window = entry.window.get();
        const Control::Ptr root = window == nullptr ? Control::Ptr{} : (*window).root();
        if (!root) {
            rest(entry);
            continue;
        }
        try {
            entry.glint_end.disconnect();
            entry.glint_end = (*window).schedule_ui_timer(*root, glint_moment, FrameClock::now() + glint_moment,
                                                          GlintEnd{this, (*window).window_ref()});
        } catch (...) {
            rest(entry);
        }
    }
}

void StockPresenter::control_changed(ControlState state, const ClientIdentity& client) {
    client_ = client;
    in_control_ = state == ControlState::model_control;
    for (BandWindow& entry : windows_) {
        Window* const window = entry.window.get();
        if (window != nullptr) show_sign(*window);
    }
    if (!in_control_) close_dialog(PresenterDialog::take_back);
    if (state != ControlState::asking) {
        countdown_tick_.disconnect();
        close_dialog(PresenterDialog::take);
    }
}

std::string StockPresenter::take_text(std::int64_t seconds) const {
    const std::string label = model_label(client_);
    const std::string text = label + " wants to use this program. While it is in control your clicks and keys go "
                                     "to a dialog that lets you take control back. Allowed in " +
                             std::to_string(std::max<std::int64_t>(seconds, 0)) + " s unless you refuse.";
    return text;
}

void StockPresenter::ask_take(const ClientIdentity& client, std::chrono::milliseconds countdown) {
    client_ = client;
    countdown_end_ = FrameClock::now() + countdown;
    std::string why{};
    const std::int64_t seconds = std::chrono::duration_cast<std::chrono::seconds>(countdown).count();
    const bool opened = open_dialog(PresenterDialog::take, "Allow " + model_label(client) + " to take control?",
                                    take_text(seconds), std::string{}, why);
    // Nobody can be asked: refused (a question that cannot be shown is
    // never Allow).
    if (!opened) {
        if (control_ != nullptr) (*control_).answer_take(false);
        return;
    }
    arm_countdown();
}

void StockPresenter::arm_countdown() noexcept {
    countdown_tick_.disconnect();
    for (const BandWindow& entry : windows_) {
        Window* const window = entry.window.get();
        const Control::Ptr root = window == nullptr ? Control::Ptr{} : (*window).root();
        if (!root) continue;
        try {
            countdown_tick_ = (*window).schedule_ui_timer(*root, countdown_tick, FrameClock::now() + countdown_tick,
                                                          CountdownTick{self_});
            return;
        } catch (...) {
            // This window cannot schedule (closing): the next one.
        }
    }
}

void StockPresenter::countdown_ticked() noexcept {
    countdown_tick_.disconnect();
    if (control_ == nullptr || (*control_).state() != ControlState::asking) return;
    const FrameTime now = FrameClock::now();
    if (now >= countdown_end_) {
        // The countdown ran out: Allow (the owner's control model).
        close_dialog(PresenterDialog::take);
        try {
            (*control_).answer_take(true);
        } catch (...) {
            // AI mode could not be shown: the control gave control back.
        }
        return;
    }
    const std::int64_t left =
        std::chrono::duration_cast<std::chrono::milliseconds>(countdown_end_ - now).count() / 1000 + 1;
    const std::shared_ptr<Label> label = countdown_label_.lock();
    if (label && dialogs_[dialog_index(PresenterDialog::take)].get() != nullptr) {
        try {
            (*label).text = take_text(left);
        } catch (...) {
            // The text could not change (no memory): the countdown runs on.
        }
    }
    arm_countdown();
}

void StockPresenter::offer_take_back(const ClientIdentity& client) {
    client_ = client;
    if (dialogs_[dialog_index(PresenterDialog::take_back)].get() != nullptr) return;
    std::string why{};
    const std::string label = model_label(client);
    static_cast<void>(open_dialog(PresenterDialog::take_back, "Do you want to take back control of this program?",
                                  "Taking back ends " + label + "'s control and interrupts its work; it is told in "
                                  "its next reply.",
                                  label + " has not yet released the application.", why));
}

void StockPresenter::ask_consent(const ClientIdentity& client) {
    client_ = client;
    const std::string label = model_label(client);
    std::string why{};
    const bool opened = open_dialog(PresenterDialog::consent, "Allow " + label + "?",
                                    "Allow " + label +
                                        " to edit files and perform potentially destructive acts? Your answer holds "
                                        "until the program closes.",
                                    std::string{}, why);
    // No windowing to ask with: the question stays unasked, and the act
    // says it could not be shown (round 2: never "the user did not allow").
    if (!opened && control_ != nullptr) (*control_).consent_not_shown(why);
}

void StockPresenter::user_step_needed(Window& window, const SemanticNode& node, std::string_view what) {
    const Control::Ptr root = window.root();
    if (unsigned_ || !root) return;
    ToolTipRequest request{};
    request.text = std::string(what);
    request.description = client_label(client_) + " is waiting for you";
    request.anchor = node.bounds;
    request.duration = stxlib::Optional<std::chrono::milliseconds>::of(std::chrono::milliseconds{0});
    try {
        static_cast<void>(window.request_tool_tip(root, std::move(request)));
    } catch (const std::invalid_argument&) {
        // Empty text or a node outside the window: the reply still says it.
    }
}

void StockPresenter::disconnected() noexcept {
    connected_ = false;
    in_control_ = false;
    countdown_tick_.disconnect();
    close_dialog(PresenterDialog::take);
    close_dialog(PresenterDialog::take_back);
    close_dialog(PresenterDialog::consent);
    for (BandWindow& entry : windows_) {
        // rest() reads connected_, now false: the plain band.
        rest(entry);
        Window* const window = entry.window.get();
        if (window == nullptr) continue;
        show_sign(*window);
    }
}

bool StockPresenter::presenter_window(const SurfaceRoot& window) const noexcept {
    for (const WindowRef& dialog : dialogs_) {
        const SurfaceRoot* const own = dialog.get();
        if (own != nullptr && own == &window) return true;
    }
    return false;
}

bool StockPresenter::dialog_open(PresenterDialog dialog) const noexcept {
    const bool open = dialogs_[dialog_index(dialog)].get() != nullptr;
    return open;
}

bool StockPresenter::open_dialog(PresenterDialog dialog, const std::string& heading, const std::string& text,
                                 const std::string& warning, std::string& why) {
    const std::size_t index = dialog_index(dialog);
    if (dialogs_[index].get() != nullptr) return true;
    if (!owner_.active()) {
        why = "the program has no window to ask from";
        return false;
    }
    const ApplicationWindows windowing = owner_.windows();
    if (!windowing.active()) {
        why = "the program's windowing has stopped";
        return false;
    }
    const char* const id = dialog_id(dialog);
    const std::shared_ptr<PresenterPanel> panel =
        make_control<PresenterPanel>(StableId(std::string(id)), this, dialog, heading, text, warning);
    if (dialog == PresenterDialog::take) countdown_label_ = (*panel).text_label();
    std::unique_ptr<Window> model = std::make_unique<Window>(panel, dialog_size);
    const WindowRef reference = (*model).window_ref();
    ApplicationWindow entry{};
    entry.stable_id = id;
    entry.owner_id = owner_id_;
    entry.tool_window = true;
    entry.model = std::move(model);
    entry.options.title = dialog_title(dialog);
    entry.options.initial_size = dialog_size;
    entry.options.minimum_size = dialog_size;
    entry.options.minimizable = false;
    entry.options.closed = DialogClosed{this, dialog};
    // Set before opening: the gate asks presenter_window for the dialog's
    // first input, which may come during open_window.
    dialogs_[index] = reference;
    const ApplicationWindowOpenResult opened = windowing.open_window(std::move(entry));
    if (!opened.status.accepted()) {
        dialogs_[index] = WindowRef{};
        why = "the program refused to open the dialog window";
        return false;
    }
    dialog_handles_[index] = opened.window;
    return true;
}

void StockPresenter::close_dialog(PresenterDialog dialog) noexcept {
    if (self_) {
        for (const BandWindow& entry : windows_) {
            Window* const window = entry.window.get();
            if (window == nullptr) continue;
            try {
                static_cast<void>((*window).begin_invoke(DeferredClose{self_, dialog}));
                return;
            } catch (...) {
                // The window stopped accepting work: close now.
                break;
            }
        }
    }
    close_dialog_now(dialog);
}

void StockPresenter::close_dialog_now(PresenterDialog dialog) noexcept {
    const std::size_t index = dialog_index(dialog);
    ApplicationWindowHandle& handle = dialog_handles_[index];
    if (!handle.active()) {
        dialogs_[index] = WindowRef{};
        return;
    }
    const ApplicationWindowHandle closing = handle;
    handle = ApplicationWindowHandle{};
    // The closed callback runs before destroy_window returns; it finds the
    // handle cleared and answers nothing.
    try {
        static_cast<void>(owner_.windows().destroy_window(closing));
    } catch (...) {
        // The run is ending: its windows close with it.
    }
    dialogs_[index] = WindowRef{};
}

void StockPresenter::dialog_closed(PresenterDialog dialog) noexcept {
    const std::size_t index = dialog_index(dialog);
    ApplicationWindowHandle& handle = dialog_handles_[index];
    const bool by_user = handle.active();
    handle = ApplicationWindowHandle{};
    dialogs_[index] = WindowRef{};
    if (!by_user || control_ == nullptr) return;
    try {
        // Closing the take question or the destructive-act question is
        // Don't Allow; closing the take-back dialog keeps waiting.
        if (dialog == PresenterDialog::take) {
            countdown_tick_.disconnect();
            (*control_).answer_take(false);
        } else if (dialog == PresenterDialog::consent) {
            if ((*control_).consent() == ConsentState::asking) (*control_).answer_consent(false);
        }
    } catch (...) {
        // The control's presenter call failed: the state already changed
        // (the control changes state first).
    }
}

void StockPresenter::dialog_chosen(PresenterDialog dialog, bool accepted) {
    close_dialog(dialog);
    if (control_ == nullptr) return;
    switch (dialog) {
    case PresenterDialog::take:
        countdown_tick_.disconnect();
        (*control_).answer_take(accepted);
        return;
    case PresenterDialog::take_back:
        if (accepted) (*control_).user_took_back();
        return;
    case PresenterDialog::consent:
        if ((*control_).consent() == ConsentState::asking) (*control_).answer_consent(accepted);
        return;
    }
}

} // namespace gui_forms::automation
