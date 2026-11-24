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

constexpr Size take_back_size{360.0, 120.0};
constexpr Size question_size{460.0, 190.0};

// A popup's content: the heading and text, and its buttons. Its buttons'
// clicks reach the presenter, which outlives the popup (it destroys its
// popups first).
class PresenterPanel final : public Control {
public:
    static constexpr bool initialize_tree_after_construction = true;

    PresenterPanel(StableId stable_id, StockPresenter* presenter, bool question, std::string heading,
                   std::string text)
        : Control(std::move(stable_id)), presenter_(presenter), question_(question), heading_(std::move(heading)),
          text_(std::move(text)) {}

    void initialize_control_tree() {
        const Size size = question_ ? question_size : take_back_size;
        const std::string base(stable_id().value());
        const std::shared_ptr<Label> heading = make_control<Label>(StableId(base + ".heading"), heading_);
        (*heading).set_bounds({20.0, 14.0, size.width - 40.0, 24.0});
        add_child(heading);
        const std::shared_ptr<Label> text = make_control<Label>(StableId(base + ".text"), text_);
        (*text).text_wrapping = TextWrapping::word;
        (*text).set_bounds({20.0, 42.0, size.width - 40.0, size.height - 98.0});
        add_child(text);
        if (question_) {
            const std::shared_ptr<Button> deny = make_control<Button>(StableId(base + ".deny"), "Don't Allow");
            (*deny).set_bounds({size.width - 250.0, size.height - 48.0, 110.0, 32.0});
            on((*deny).clicked(), *this, &PresenterPanel::deny_clicked);
            add_child(deny);
            const std::shared_ptr<Button> allow = make_control<Button>(StableId(base + ".allow"), "Allow");
            (*allow).set_bounds({size.width - 130.0, size.height - 48.0, 110.0, 32.0});
            on((*allow).clicked(), *this, &PresenterPanel::allow_clicked);
            add_child(allow);
            return;
        }
        const std::shared_ptr<Button> take = make_control<Button>(StableId(base + ".take-back"), "Take back control");
        (*take).set_bounds({size.width - 190.0, size.height - 48.0, 170.0, 32.0});
        on((*take).clicked(), *this, &PresenterPanel::take_back_clicked);
        add_child(take);
    }

    void take_back_clicked(ButtonBase&) {
        if (presenter_ != nullptr) (*presenter_).take_back_chosen();
    }
    void allow_clicked(ButtonBase&) {
        if (presenter_ != nullptr) (*presenter_).consent_chosen(true);
    }
    void deny_clicked(ButtonBase&) {
        if (presenter_ != nullptr) (*presenter_).consent_chosen(false);
    }

private:
    StockPresenter* presenter_{};
    bool question_{};
    std::string heading_{};
    std::string text_{};
};

// A popup's closed callback (ApplicationWindowOptions::closed).
struct PopupClosed final {
    StockPresenter* presenter{};
    bool question{};
    void operator()() const noexcept { (*presenter).popup_closed(question); }
};

// A popup closes at its own dispatcher turn: never from inside one of its
// own button's clicks, which run on the popup's window.
struct DeferredClose final {
    std::weak_ptr<StockPresenter*> presenter{};
    bool question{};
    void operator()() const noexcept {
        const std::shared_ptr<StockPresenter*> live = presenter.lock();
        if (live) (**live).close_popup_now(question);
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
    close_popup_now(true);
    close_popup_now(false);
    close_popup(true);
    close_popup(false);
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
    if (!connected_) return;
    show_title_note(window, true);
    show_tag_line(window, tag_line_moment);
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

void StockPresenter::show_title_note(Window& window, bool shown) noexcept {
    try {
        // The wash goes with the note: the window's content, mostly grey,
        // says a model drives it away from the border too.
        double saturation = 1.0;
        const Control::Ptr root = window.root();
        if (shown && root) saturation = (*root).effective_theme().definition().model_wash_saturation;
        window.set_content_wash(shown ? saturation : 1.0);
        // Windows 11's own border joins the sign in the accent.
        stxlib::Optional<Color> border{};
        if (shown && root) border = stxlib::Optional<Color>::of((*root).effective_theme().basic_style().accent);
        window.set_system_border_colour(border);
        if (!shown) {
            window.set_title_note(nullptr);
            return;
        }
        const std::string note = " \u2014 " + model_label(client_) + " is using this";
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
        show_title_note(window, false);
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
        show_title_note(*window, true);
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
    if (state == ControlState::model_directed) {
        if (!take_back_.get()) {
            static_cast<void>(open_popup(false, client_label(client) + " is in control",
                                         "Your input is paused while it works. Take back control at any time."));
        }
        return;
    }
    // user_control (and input_suspended, which shows no popup: study 8.2).
    close_popup(false);
}

void StockPresenter::ask_consent(const ClientIdentity& client) {
    client_ = client;
    const std::string label = client_label(client);
    const bool opened = open_popup(true, "Allow " + label + "?",
                                   "Allow " + label +
                                       " to edit files and perform potentially destructive acts? Your answer holds "
                                       "until the program closes.");
    // No windowing to ask with: Don't Allow (study 10: closing is Don't
    // Allow, and a question that cannot be shown is never Allow).
    if (!opened && control_ != nullptr) (*control_).answer_consent(false);
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
    close_popup(false);
    close_popup(true);
    for (BandWindow& entry : windows_) {
        // rest() reads connected_, now false: the plain band.
        rest(entry);
        Window* const window = entry.window.get();
        if (window == nullptr) continue;
        show_title_note(*window, false);
    }
}

bool StockPresenter::presenter_window(const SurfaceRoot& window) const noexcept {
    const SurfaceRoot* const take_back = take_back_.get();
    const SurfaceRoot* const question = question_.get();
    const bool own = (take_back != nullptr && take_back == &window) || (question != nullptr && question == &window);
    return own;
}

bool StockPresenter::open_popup(bool question, const std::string& heading, const std::string& text) {
    if (!owner_.active()) return false;
    const ApplicationWindows windowing = owner_.windows();
    if (!windowing.active()) return false;
    const Size size = question ? question_size : take_back_size;
    const char* const id = question ? "automation.question" : "automation.take-back";
    const Control::Ptr panel = make_control<PresenterPanel>(StableId(std::string(id)), this, question, heading, text);
    std::unique_ptr<Window> model = std::make_unique<Window>(panel, size);
    const WindowRef reference = (*model).window_ref();
    ApplicationWindow entry{};
    entry.stable_id = id;
    entry.owner_id = owner_id_;
    entry.tool_window = true;
    entry.model = std::move(model);
    entry.options.title = question ? "Allow destructive acts?" : "A model is in control";
    entry.options.initial_size = size;
    entry.options.minimum_size = size;
    entry.options.minimizable = false;
    entry.options.closed = PopupClosed{this, question};
    // Set before opening: the gate asks presenter_window for the popup's
    // first input, which may come during open_window.
    (question ? question_ : take_back_) = reference;
    const ApplicationWindowOpenResult opened = windowing.open_window(std::move(entry));
    if (!opened.status.accepted()) {
        (question ? question_ : take_back_) = WindowRef{};
        return false;
    }
    (question ? question_handle_ : take_back_handle_) = opened.window;
    return true;
}

void StockPresenter::close_popup(bool question) noexcept {
    if (self_) {
        for (const BandWindow& entry : windows_) {
            Window* const window = entry.window.get();
            if (window == nullptr) continue;
            try {
                static_cast<void>((*window).begin_invoke(DeferredClose{self_, question}));
                return;
            } catch (...) {
                // The window stopped accepting work: close now.
                break;
            }
        }
    }
    close_popup_now(question);
}

void StockPresenter::close_popup_now(bool question) noexcept {
    ApplicationWindowHandle& handle = question ? question_handle_ : take_back_handle_;
    if (!handle.active()) {
        (question ? question_ : take_back_) = WindowRef{};
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
    (question ? question_ : take_back_) = WindowRef{};
}

void StockPresenter::popup_closed(bool question) noexcept {
    ApplicationWindowHandle& handle = question ? question_handle_ : take_back_handle_;
    const bool by_user = handle.active();
    handle = ApplicationWindowHandle{};
    (question ? question_ : take_back_) = WindowRef{};
    if (!by_user || control_ == nullptr) return;
    try {
        // Closing the question is Don't Allow; closing the take-back popup
        // takes control back (study 8.3, step 3).
        if (question) {
            if ((*control_).consent() == ConsentState::asking) (*control_).answer_consent(false);
        } else {
            (*control_).user_took_back();
        }
    } catch (...) {
        // The control's presenter call failed: the state already changed
        // (the control changes state first).
    }
}

void StockPresenter::take_back_chosen() {
    // The control changes state first and then tells control_changed, which
    // closes the popup at a later turn.
    if (control_ != nullptr) (*control_).user_took_back();
    close_popup(false);
}

void StockPresenter::consent_chosen(bool allow) {
    close_popup(true);
    if (control_ != nullptr && (*control_).consent() == ConsentState::asking) (*control_).answer_consent(allow);
}

} // namespace gui_forms::automation
