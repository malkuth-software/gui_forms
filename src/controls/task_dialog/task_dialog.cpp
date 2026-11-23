#include "gui_forms/controls/task_dialog.hpp"
#include "../frame_chrome/frame_chrome.hpp"

#include "gui_forms/controls/progress_bar.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/resources/stock_icons/stock_icons.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <stxlib/optional.hpp>
#include <stxlib/result.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

// Where every part and child sits, from one pass over a width.
struct TaskDialog::Layout final {
    Parts parts{};
    // Child slots, in the order of buttons_ and radios_.
    std::vector<Rect> buttons{};
    std::vector<Rect> radios{};
    double height{};
};

struct TaskDialog::State final {
    std::vector<std::shared_ptr<Button>> buttons_{};
    std::vector<std::shared_ptr<RadioButton>> radios_{};
    std::shared_ptr<Label> heading_{};
    std::shared_ptr<Label> text_{};
    std::shared_ptr<Label> expanded_text_{};
    std::shared_ptr<Label> footnote_{};
    std::shared_ptr<CheckBox> verification_{};
    std::shared_ptr<Button> expander_button_{};
    std::shared_ptr<ProgressBar> progress_{};
    std::shared_ptr<TextBox> input_{};
    std::vector<SubscriptionToken> child_tokens_{};
    std::optional<TaskDialogResult> result_{};
    Event<const std::string&> radio_button_changed_{};
    [[nodiscard]] static stxlib::Optional<std::size_t> button_index(const TaskDialog& self,
                                                                  std::string_view button_id) noexcept;
    [[nodiscard]] static std::string default_button_id(const TaskDialog& self);
    [[nodiscard]] static std::string cancel_button_id(const TaskDialog& self);
    // Adds a word-wrapped, top-aligned Label child named <stable_id><suffix>
    // showing `text` in `font`: the heading, text, expanded text and
    // footnote paragraphs.
    [[nodiscard]] static std::shared_ptr<Label> make_paragraph(TaskDialog& self, std::string_view suffix,
                                                               const std::string& text, FontSpec font);
    static void finish(TaskDialog& self, std::string button_id);
};

stxlib::Unique<TaskDialog::State> TaskDialog::make_state() {
    stxlib::OperationResult<stxlib::Unique<State>> made = stxlib::Unique<State>::make();
    if (made.failed()) throw std::bad_alloc();
    stxlib::Unique<State> state = std::move(made).take_value();
    return state;
}

std::shared_ptr<CheckBox> TaskDialog::verification_check_box() const noexcept {
    return (*state_).verification_;
}

std::shared_ptr<Button> TaskDialog::expander_button() const noexcept {
    return (*state_).expander_button_;
}

std::shared_ptr<ProgressBar> TaskDialog::progress_bar() const noexcept {
    return (*state_).progress_;
}

std::shared_ptr<TextBox> TaskDialog::input_box() const noexcept {
    return (*state_).input_;
}

const std::optional<TaskDialogResult>& TaskDialog::result() const noexcept {
    return (*state_).result_;
}

Event<const std::string&>& TaskDialog::radio_button_changed() noexcept {
    return (*state_).radio_button_changed_;
}

namespace {

// Geometry in logical units at text scale 1.
constexpr double preferred_dialog_width = 440.0;
constexpr double minimum_dialog_width = 240.0;
constexpr double window_margin = 16.0;
constexpr double content_padding = 16.0;
constexpr double caption_height = 28.0;
constexpr double icon_size = 32.0;
constexpr double icon_gap = 12.0;
constexpr double paragraph_gap = 8.0;
constexpr double progress_height = 16.0;
constexpr double input_height = 28.0;
constexpr double radio_height = 24.0;
constexpr double expander_height = 28.0;
constexpr double expander_glyph_size = 10.0;
constexpr double expander_glyph_gap = 4.0;
constexpr double verification_height = 24.0;
constexpr double button_height = 28.0;
constexpr double minimum_button_width = 80.0;
constexpr double button_text_padding = 24.0;
constexpr double button_gap = 8.0;
constexpr double band_padding = 12.0;
constexpr double footnote_icon_size = 16.0;
constexpr double footnote_padding = 10.0;
constexpr double heading_size = 14.0;
constexpr double footnote_size = 10.0;

// `stable_id` is the name results and events report (it matches
// DialogResult's names); `segment` is the button control's stable-ID segment.
// They differ only for Try Again, whose name has an underscore, which a
// stable ID does not admit (A-gf-controls-13-F1).
struct StandardButtonName final {
    TaskDialogStandardButton standard{TaskDialogStandardButton::ok};
    std::string_view stable_id{};
    std::string_view text{};
    std::string_view segment{};
};

constexpr std::array<StandardButtonName, 11> standard_buttons{{
    {TaskDialogStandardButton::ok, "ok", "OK", "ok"},
    {TaskDialogStandardButton::cancel, "cancel", "Cancel", "cancel"},
    {TaskDialogStandardButton::yes, "yes", "Yes", "yes"},
    {TaskDialogStandardButton::no, "no", "No", "no"},
    {TaskDialogStandardButton::retry, "retry", "Retry", "retry"},
    {TaskDialogStandardButton::abort, "abort", "Abort", "abort"},
    {TaskDialogStandardButton::ignore, "ignore", "Ignore", "ignore"},
    {TaskDialogStandardButton::try_again, "try_again", "Try Again", "try-again"},
    {TaskDialogStandardButton::continue_, "continue", "Continue", "continue"},
    {TaskDialogStandardButton::close, "close", "Close", "close"},
    {TaskDialogStandardButton::help, "help", "Help", "help"},
}};

// The stable-ID segment of the control for a button named `button_id`: a
// standard name's segment, otherwise the name itself.
std::string_view button_segment(std::string_view button_id) noexcept {
    for (const StandardButtonName& name : standard_buttons) {
        if (name.stable_id == button_id) return name.segment;
    }
    return button_id;
}

const StandardButtonName* standard_name(TaskDialogStandardButton standard) noexcept {
    for (const StandardButtonName& name : standard_buttons) {
        if (name.standard == standard) return &name;
    }
    return nullptr;
}

bool valid_text(std::string_view text) {
    const bool result = validate_utf8(text).valid();
    return result;
}

// The stock icon that draws a standard icon, and the surface state whose
// glyph colour it takes (the colours MessageBox's alert icons use).
struct StandardIcon final {
    std::string_view name{};
    ControlSurfaceState surface{ControlSurfaceState::normal};
};

StandardIcon standard_icon(TaskDialogIcon icon) noexcept {
    switch (icon) {
    case TaskDialogIcon::information:
        return StandardIcon{stock_icon_information, ControlSurfaceState::normal};
    case TaskDialogIcon::warning:
        return StandardIcon{stock_icon_warning, ControlSurfaceState::pending};
    case TaskDialogIcon::error:
        return StandardIcon{"x-circle", ControlSurfaceState::invalid};
    case TaskDialogIcon::shield:
        return StandardIcon{"shield-check", ControlSurfaceState::normal};
    case TaskDialogIcon::none:
    default:
        // Fallback: no icon, and a value outside the enumeration draws none.
        return StandardIcon{};
    }
}

Rect bounds_union(Rect left, Rect right) noexcept {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const double x = std::min(left.x, right.x);
    const double y = std::min(left.y, right.y);
    const double right_edge = std::max(left.x + left.width, right.x + right.width);
    const double bottom = std::max(left.y + left.height, right.y + right.height);
    const Rect result{x, y, right_edge - x, bottom - y};
    return result;
}

// The full-client layer a shown dialog sits in. It takes every pointer press
// outside the dialog, which makes the dialog modal within its window, and
// centres the dialog in the client area.
class TaskDialogLayer final : public Control {
public:
    explicit TaskDialogLayer(StableId stable_id) : Control(std::move(stable_id)) {
        set_paint_plane(PaintPlane::overlay);
    }

    [[nodiscard]] Size measure(Size available) override { return available; }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        const double scale = effective_text_scale();
        const double margin = window_margin * scale;
        const Size available{std::max(0.0, final_bounds.width - margin * 2.0),
                             std::max(0.0, final_bounds.height - margin * 2.0)};
        const std::vector<Control::Ptr> children = snapshot_layout_children();
        for (const Control::Ptr& child : children) {
            if (!is_current_layout_child(child)) continue;
            const Size wanted = (*child).measure(available);
            const double width = std::min(wanted.width, available.width);
            const double height = std::min(wanted.height, std::max(0.0, final_bounds.height));
            const Rect slot{std::floor((final_bounds.width - width) * 0.5),
                            std::max(0.0, std::floor((final_bounds.height - height) * 0.5)), width, height};
            set_child_layout(child, slot);
        }
    }

    [[nodiscard]] bool hit_test_local(Point) const override { return true; }

    // The dialog's backdrop swallows presses, releases and wheels.
    void on_mouse_down(MouseEventArgs& arguments) override {
        arguments.handled = true;
        Control::on_mouse_down(arguments);
    }

    void on_mouse_up(MouseEventArgs& arguments) override {
        arguments.handled = true;
        Control::on_mouse_up(arguments);
    }

    void on_mouse_wheel(MouseEventArgs& arguments) override {
        arguments.handled = true;
        Control::on_mouse_wheel(arguments);
    }
};

} // namespace

TaskDialog::TaskDialog(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    cursor = stxlib::Optional<CursorKind>::of(CursorKind::arrow);
}

TaskDialog::~TaskDialog() {
    overlay_revoked_.disconnect();
    native_closed_.disconnect();
    native_.disconnect();
    overlay_.disconnect();
}

// -- page ----------------------------------------------------------------------

void TaskDialog::set_page(TaskDialogPage page) {
    require_mutable();
    if (!valid_text(page.caption) || !valid_text(page.heading) || !valid_text(page.text) ||
        !valid_text(page.verification_text) || !valid_text(page.footnote) || !valid_text(page.default_button)) {
        throw std::invalid_argument("TaskDialog page text must be valid UTF-8");
    }
    if (page.buttons.empty()) {
        page.buttons.push_back({.standard = TaskDialogStandardButton::ok});
    }
    std::unordered_set<std::string> identities{};
    for (TaskDialogButton& button : page.buttons) {
        const StandardButtonName* const name = standard_name(button.standard);
        if (button.stable_id.empty() && name != nullptr) button.stable_id = std::string((*name).stable_id);
        if (button.text.empty() && name != nullptr) button.text = std::string((*name).text);
        if (button.stable_id.empty() || !valid_text(button.stable_id) || !valid_text(button.text) ||
            !identities.insert(button.stable_id).second) {
            throw std::invalid_argument("TaskDialog buttons require unique, non-empty IDs and valid UTF-8");
        }
    }
    // Each button becomes a control named prefix.button.<segment>: the
    // segment must be a stable ID and unique, checked here before page_ or
    // the children change (A-gf-controls-13-F1).
    identities.clear();
    for (const TaskDialogButton& button : page.buttons) {
        const std::string_view segment = button_segment(button.stable_id);
        if (!valid_stable_id(segment) || !identities.insert(std::string(segment)).second) {
            throw std::invalid_argument("TaskDialog button IDs must be stable-ID segments ([a-z0-9] runs joined by '-' or '.')");
        }
    }
    identities.clear();
    std::size_t checked = 0U;
    for (const TaskDialogRadioButton& radio : page.radio_buttons) {
        if (radio.stable_id.empty() || !valid_text(radio.stable_id) || !valid_text(radio.text) ||
            !identities.insert(radio.stable_id).second) {
            throw std::invalid_argument("TaskDialog radio buttons require unique, non-empty IDs and valid UTF-8");
        }
        if (!valid_stable_id(radio.stable_id)) {
            throw std::invalid_argument("TaskDialog radio button IDs must be stable-ID segments ([a-z0-9] runs joined by '-' or '.')");
        }
        if (radio.checked) ++checked;
    }
    if (checked > 1U) throw std::invalid_argument("TaskDialog checks at most one radio button");
    if (!page.default_button.empty()) {
        bool found = false;
        for (const TaskDialogButton& button : page.buttons) {
            if (button.stable_id == page.default_button) {
                found = true;
                break;
            }
        }
        if (!found) throw std::invalid_argument("TaskDialog default button is not on the page");
    }
    if (page.expander) {
        const TaskDialogExpander& expander = *page.expander;
        if (!valid_text(expander.text) || !valid_text(expander.collapsed_button_text) ||
            !valid_text(expander.expanded_button_text)) {
            throw std::invalid_argument("TaskDialog expander text must be valid UTF-8");
        }
    }
    if (page.input.has_value()) {
        const TaskDialogInput& input = *page.input;
        if (!valid_text(input.text) || !valid_text(input.placeholder)) {
            throw std::invalid_argument("TaskDialog input text must be valid UTF-8");
        }
        if (input.maximum_length != 0U && input.text.size() > input.maximum_length) {
            throw std::invalid_argument("TaskDialog input text is longer than its maximum length");
        }
    }
    if (page.progress_bar.present()) {
        const TaskDialogProgressBar& progress = (*page.progress_bar.value());
        if (!std::isfinite(progress.minimum) || !std::isfinite(progress.maximum) ||
            !std::isfinite(progress.value) || progress.minimum >= progress.maximum) {
            throw std::invalid_argument("TaskDialog progress bounds must be finite and ordered");
        }
    }
    page_ = std::move(page);
    rebuild_children();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if (!shown_) return;
    // A page shown in place of another: focus moves to its default button.
    Window* const host = attached_window();
    const std::shared_ptr<Button> preferred = button(State::default_button_id(*this));
    if (host != nullptr && preferred) static_cast<void>((*host).request_focus(preferred));
    navigated_.emit();
}

void TaskDialog::clear_children_of_page() noexcept {
    State& state = *state_;
    state.child_tokens_.clear();
    state.buttons_.clear();
    state.radios_.clear();
    state.heading_.reset();
    state.text_.reset();
    state.expanded_text_.reset();
    state.footnote_.reset();
    state.verification_.reset();
    state.expander_button_.reset();
    state.progress_.reset();
    state.input_.reset();
}

std::shared_ptr<Label> TaskDialog::State::make_paragraph(TaskDialog& self, std::string_view suffix,
                                                         const std::string& text, FontSpec font) {
    std::string identity(self.stable_id().value());
    identity.append(suffix);
    std::shared_ptr<Label> paragraph = make_control<Label>(StableId(std::move(identity)), text);
    (*paragraph).font = stxlib::Optional<FontSpec>::of(font);
    (*paragraph).text_wrapping = TextWrapping::word;
    (*paragraph).vertical_alignment = VerticalAlignment::start;
    self.add_child(paragraph);
    return paragraph;
}

void TaskDialog::rebuild_children() {
    State& state = *state_;
    clear_children_of_page();
    clear_children();
    const std::string prefix(stable_id().value());
    FontSpec heading_font = font_;
    heading_font.size = heading_size;
    heading_font.weight = 600;
    FontSpec footnote_font = font_;
    footnote_font.size = footnote_size;
    if (!page_.heading.empty()) state.heading_ = State::make_paragraph(*this, ".heading", page_.heading, heading_font);
    if (!page_.text.empty()) state.text_ = State::make_paragraph(*this, ".text", page_.text, font_);
    if (page_.input.has_value()) {
        const TaskDialogInput& settings = *page_.input;
        state.input_ = make_control<TextBox>(StableId(prefix + ".input"), settings.text);
        (*state.input_).placeholder_text = settings.placeholder;
        (*state.input_).maximum_length = settings.maximum_length;
        // The prompt names the field to assistive technology.
        (*state.input_).accessible_name = page_.text;
        add_child(state.input_);
    }
    if (page_.progress_bar.present()) {
        const TaskDialogProgressBar& settings = (*page_.progress_bar.value());
        state.progress_ = make_control<ProgressBar>(StableId(prefix + ".progress"));
        (*state.progress_).set_range(settings.minimum, settings.maximum);
        (*state.progress_).value = std::clamp(settings.value, settings.minimum, settings.maximum);
        if (settings.marquee) (*state.progress_).visual_style = ProgressBarVisualStyle::marquee;
        add_child(state.progress_);
    }
    for (const TaskDialogRadioButton& radio : page_.radio_buttons) {
        std::shared_ptr<RadioButton> control =
            make_control<RadioButton>(StableId(prefix + ".radio." + radio.stable_id), radio.text);
        (*control).checked = radio.checked;
        (*control).enabled = radio.enabled;
        add_child(control);
        state.child_tokens_.push_back((*control).checked_changed().subscribe(
            *this, Delegate<bool>::bind<TaskDialog, &TaskDialog::on_radio>(*this)));
        state.radios_.push_back(std::move(control));
    }
    if (page_.expander) {
        const TaskDialogExpander& expander = *page_.expander;
        state.expander_button_ = make_control<Button>(
            StableId(prefix + ".expander"),
            expander.expanded ? expander.expanded_button_text : expander.collapsed_button_text);
        (*state.expander_button_).visual_style = ButtonVisualStyle::flat;
        add_child(state.expander_button_);
        state.child_tokens_.push_back((*state.expander_button_).clicked().subscribe(
            *this, Delegate<ButtonBase&>::bind<TaskDialog, &TaskDialog::on_expander>(*this)));
        state.expanded_text_ = State::make_paragraph(*this, ".expanded-text", expander.text, font_);
        set_child_layout_collapsed(state.expanded_text_, !expander.expanded);
    }
    if (!page_.verification_text.empty()) {
        state.verification_ = make_control<CheckBox>(StableId(prefix + ".verification"), page_.verification_text);
        (*state.verification_).checked = page_.verification_checked;
        add_child(state.verification_);
        state.child_tokens_.push_back((*state.verification_).checked_changed().subscribe(
            *this, Delegate<bool>::bind<TaskDialog, &TaskDialog::on_verification>(*this)));
    }
    const std::string preferred = State::default_button_id(*this);
    for (const TaskDialogButton& definition : page_.buttons) {
        std::shared_ptr<Button> control =
            make_control<Button>(StableId(prefix + ".button." + std::string(button_segment(definition.stable_id))),
                                 definition.text);
        (*control).enabled = definition.enabled;
        if (definition.stable_id == preferred) (*control).visual_style = ButtonVisualStyle::accent;
        add_child(control);
        state.child_tokens_.push_back((*control).clicked().subscribe(
            *this, Delegate<ButtonBase&>::bind<TaskDialog, &TaskDialog::on_child_button>(*this)));
        state.buttons_.push_back(std::move(control));
    }
    if (!page_.footnote.empty()) {
        state.footnote_ = State::make_paragraph(*this, ".footnote", page_.footnote, footnote_font);
    }
}

stxlib::Optional<std::size_t> TaskDialog::State::button_index(const TaskDialog& self,
                                                             std::string_view button_id) noexcept {
    for (std::size_t index = 0; index < self.page_.buttons.size(); ++index) {
        if (self.page_.buttons[index].stable_id == button_id) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

std::string TaskDialog::State::default_button_id(const TaskDialog& self) {
    if (!self.page_.default_button.empty()) return self.page_.default_button;
    if (self.page_.buttons.empty()) return {};
    return self.page_.buttons.front().stable_id;
}

std::string TaskDialog::State::cancel_button_id(const TaskDialog& self) {
    for (const TaskDialogButton& definition : self.page_.buttons) {
        if (definition.standard == TaskDialogStandardButton::cancel) return definition.stable_id;
    }
    return "cancel";
}

std::shared_ptr<Button> TaskDialog::button(std::string_view button_id) const {
    const stxlib::Optional<std::size_t> index = State::button_index(*this, button_id);
    if (index.absent() || (*index.value()) >= (*state_).buttons_.size()) return nullptr;
    return (*state_).buttons_[(*index.value())];
}

std::shared_ptr<RadioButton> TaskDialog::radio_button(std::string_view radio_id) const {
    for (std::size_t index = 0; index < page_.radio_buttons.size() && index < (*state_).radios_.size(); ++index) {
        if (page_.radio_buttons[index].stable_id == radio_id) return (*state_).radios_[index];
    }
    return nullptr;
}

std::string TaskDialog::input_text() const {
    if (!(*state_).input_) return std::string();
    std::string text = (*(*state_).input_).text.read();
    return text;
}

std::string TaskDialog::checked_radio_button() const {
    for (const TaskDialogRadioButton& radio : page_.radio_buttons) {
        if (radio.checked) return radio.stable_id;
    }
    return {};
}

// The expander and verification views: the page holds both states.
bool TaskDialog::read_view(const decltype(expanded)&) const {
    const bool result = page_.expander && (*page_.expander).expanded;
    return result;
}

void TaskDialog::write_view(const decltype(expanded)&, bool incoming) {
    State& state = *state_;
    require_mutable();
    if (!page_.expander || (*page_.expander).expanded == incoming) return;
    TaskDialogExpander& expander = *page_.expander;
    expander.expanded = incoming;
    if (state.expander_button_) {
        (*state.expander_button_).text = incoming ? expander.expanded_button_text : expander.collapsed_button_text;
    }
    if (state.expanded_text_) set_child_layout_collapsed(state.expanded_text_, !incoming);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    // The layer centres the dialog again at its new height.
    if (layer_) (*layer_).invalidate(Dirty::arrange);
    expanded_changed_.emit(incoming);
}

bool TaskDialog::read_view(const decltype(verification_checked)&) const {
    const bool result = page_.verification_checked;
    return result;
}

// While the check box exists the write goes through it, and its change
// reaches on_verification, which stores the state and raises
// verification_changed.
void TaskDialog::write_view(const decltype(verification_checked)&, bool incoming) {
    require_mutable();
    if ((*state_).verification_) {
        (*(*state_).verification_).checked = incoming;
        return;
    }
    page_.verification_checked = incoming;
}

void TaskDialog::set_progress_value(double value) {
    require_mutable();
    if (page_.progress_bar.absent() || !(*state_).progress_ || !std::isfinite(value)) return;
    TaskDialogProgressBar& settings = (*page_.progress_bar.value());
    settings.value = std::clamp(value, settings.minimum, settings.maximum);
    (*(*state_).progress_).value = settings.value;
}

// -- interaction ---------------------------------------------------------------

void TaskDialog::on_child_button(ButtonBase& sender) {
    for (std::size_t index = 0; index < (*state_).buttons_.size(); ++index) {
        if ((*state_).buttons_[index].get() == &sender) {
            const std::string identity = page_.buttons[index].stable_id;
            static_cast<void>(click(identity));
            return;
        }
    }
}

bool TaskDialog::click(std::string_view button_id) {
    require_mutable();
    const stxlib::Optional<std::size_t> index = State::button_index(*this, button_id);
    if (index.absent() || !page_.buttons[(*index.value())].enabled) return false;
    TaskDialogButtonClick request{std::string(button_id), !page_.buttons[(*index.value())].allow_close};
    // A handler may drop the dialog's last other reference; it stays alive
    // for the checks below (A-gf-controls-13-F6).
    const Control::Ptr lifetime = weak_from_this().lock();
    button_clicked_.emit(request);
    // A handler may have navigated to another page or closed the dialog.
    if (!is_alive() || request.keep_open || !shown_) return true;
    State::finish(*this, request.button_id);
    return true;
}

void TaskDialog::on_expander(ButtonBase&) {
    expanded = !expanded.read();
}

void TaskDialog::on_verification(bool checked) {
    if (page_.verification_checked == checked) return;
    page_.verification_checked = checked;
    verification_changed_.emit(checked);
}

void TaskDialog::on_radio(bool) {
    State& state = *state_;
    if (synchronizing_) return;
    std::string newly_checked{};
    for (std::size_t index = 0; index < state.radios_.size() && index < page_.radio_buttons.size(); ++index) {
        const bool checked = (*state.radios_[index]).checked.read();
        if (checked && !page_.radio_buttons[index].checked) newly_checked = page_.radio_buttons[index].stable_id;
        page_.radio_buttons[index].checked = checked;
    }
    if (!newly_checked.empty()) state.radio_button_changed_.emit(newly_checked);
}

void TaskDialog::on_key_preview(KeyEvent& event) {
    if (!shown_ || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::escape) {
        const std::string cancel = State::cancel_button_id(*this);
        if (State::button_index(*this, cancel).present()) {
            static_cast<void>(click(cancel));
        } else if (page_.allow_cancel) {
            State::finish(*this, cancel);
        }
        event.handled = true;
        return;
    }
    if (event.physical_key != PhysicalKey::enter) return;
    // A focused button takes its own Enter; elsewhere Enter is the default
    // button's.
    Window* const host = attached_window();
    const Control::Ptr focused = host != nullptr ? (*host).focused_control() : nullptr;
    for (const std::shared_ptr<Button>& candidate : (*state_).buttons_) {
        if (focused == candidate) return;
    }
    if (focused && focused == (*state_).expander_button_) return;
    static_cast<void>(click(State::default_button_id(*this)));
    event.handled = true;
}

// -- showing -------------------------------------------------------------------

bool TaskDialog::show(const Control::Ptr& owner) {
    State& state = *state_;
    require_mutable();
    Window* const window = owner ? (*owner).attached_window() : nullptr;
    if (shown_ || window == nullptr || parent()) return false;
    Window& host = *window;
    // A page never set has no buttons; setting it gives it the OK button.
    if (page_.buttons.empty()) set_page(page_);
    const Size client = host.client_size();
    Control::Ptr layer = make_control<TaskDialogLayer>(StableId(std::string(stable_id().value()) + ".layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    (*layer).add_child(shared_from_this());
    overlay_ = host.open_overlay(owner, layer);
    layer_ = layer;
    shown_ = true;
    state.result_.reset();
    if (Event<>* revoked = overlay_.closed_event()) {
        overlay_revoked_ = (*revoked).subscribe(*this, Delegate<>::bind<TaskDialog, &TaskDialog::on_overlay_revoked>(*this));
    }
    // begin_focus_scope moves the focus, running the previous control's
    // focus and validation handlers; one may close the dialog, and then the
    // scope it opened is ended here, since finish found none to end
    // (A-gf-controls-13-F4). `lifetime` holds the dialog across them.
    const Control::Ptr lifetime = shared_from_this();
    FocusScopeId started{};
    if (surface_ == DialogSurface::native_popup && open_native(host, owner)) {
        // The empty layer keeps the owner's keys from its controls.
        started = host.begin_focus_scope(layer);
    } else {
        const Control::Ptr first =
            state.input_ ? Control::Ptr(state.input_) : Control::Ptr(button(State::default_button_id(*this)));
        started = host.begin_focus_scope(layer, first);
    }
    if (!is_alive() || !shown_) {
        if (started) static_cast<void>(host.end_focus_scope(started));
        return true;
    }
    scope_ = started;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
    return true;
}

bool TaskDialog::open_native(Window& host, const Control::Ptr& owner) {
    // Measured in the layer, as the overlay would be, then centred.
    const Size client = host.client_size();
    const double margin = window_margin * effective_text_scale();
    const Size available{std::max(0.0, client.width - margin * 2.0), std::max(0.0, client.height - margin * 2.0)};
    const Size wanted = measure(available);
    const double width = std::max(1.0, std::min(wanted.width, available.width));
    const double height = std::max(1.0, std::min(wanted.height, client.height));
    const Rect bounds{std::floor((client.width - width) * 0.5), std::max(0.0, std::floor((client.height - height) * 0.5)),
                      width, height};
    const Control::Ptr self = shared_from_this();
    static_cast<void>((*layer_).remove_child(runtime_id()));
    native_root_ = make_control<Control>(StableId(std::string(stable_id().value()) + ".native"));
    set_requested_bounds({0.0, 0.0, width, height});
    (*native_root_).add_child(self);
    NativePopupOptions options{};
    options.focus = HostPopupFocus::takes_focus;
    options.fallback = NativePopupFallback::refuse;
    native_ = host.open_native_popup(owner, native_root_, bounds, options);
    if (native_.surface() != NativePopupSurface::native) {
        static_cast<void>((*native_root_).remove_child(runtime_id()));
        native_root_.reset();
        (*layer_).add_child(self);
        return false;
    }
    if (Event<>* closed = native_.closed_event()) {
        native_closed_ = (*closed).subscribe(*this, Delegate<>::bind<TaskDialog, &TaskDialog::on_overlay_revoked>(*this));
    }
    Window* const popup = native_.window();
    if (popup != nullptr) {
        static_cast<void>((*popup).begin_focus_scope(native_root_, button(State::default_button_id(*this))));
    }
    return true;
}

NativePopupSurface TaskDialog::presented_surface() const noexcept {
    if (!shown_) return NativePopupSurface::none;
    if (native_root_) return NativePopupSurface::native;
    return NativePopupSurface::overlay;
}

void TaskDialog::close(std::string_view button_id) {
    require_mutable();
    State::finish(*this, std::string(button_id));
}

void TaskDialog::on_overlay_revoked() {
    // The owner left its window: the dialog closes as cancelled.
    State::finish(*this, State::cancel_button_id(*this));
}

void TaskDialog::State::finish(TaskDialog& self, std::string button_id) {
    if (!self.shown_) return;
    self.shown_ = false;
    TaskDialogResult outcome{std::move(button_id), self.page_.verification_checked, self.checked_radio_button(),
                             self.input_text()};
    // The scope is the owner's window's, where the layer is.
    Window* const host = self.layer_ ? (*self.layer_).attached_window() : nullptr;
    if (host != nullptr && self.scope_) static_cast<void>((*host).end_focus_scope(self.scope_));
    self.scope_ = {};
    self.overlay_revoked_.disconnect();
    self.native_closed_.disconnect();
    const Control::Ptr layer = self.layer_;
    self.layer_.reset();
    if (layer) static_cast<void>((*layer).remove_child(self.runtime_id()));
    const Control::Ptr native_root = self.native_root_;
    self.native_root_.reset();
    if (native_root) static_cast<void>((*native_root).remove_child(self.runtime_id()));
    self.native_.disconnect();
    self.overlay_.disconnect();
    (*self.state_).result_ = outcome;
    self.closed_.emit(outcome);
}

void TaskDialog::on_dispose() noexcept {
    // A disposed dialog closes without raising closed.
    shown_ = false;
    overlay_revoked_.disconnect();
    native_closed_.disconnect();
    Window* const host = layer_ ? (*layer_).attached_window() : nullptr;
    if (scope_ && host != nullptr) {
        try {
            static_cast<void>((*host).end_focus_scope(scope_));
        } catch (...) {
            // Disposal must not throw; the window ends the scope with the overlay.
        }
    }
    scope_ = {};
    native_.disconnect();
    native_root_.reset();
    overlay_.disconnect();
    layer_.reset();
    (*state_).child_tokens_.clear();
    Control::on_dispose();
}

// -- layout --------------------------------------------------------------------

double TaskDialog::preferred_width(Size available) const noexcept {
    const double scale = effective_text_scale();
    const double width = std::min(preferred_dialog_width * scale, available.width);
    const double result = std::max(std::min(minimum_dialog_width * scale, available.width), width);
    return result;
}

TaskDialog::Layout TaskDialog::layout(double width) const {
    State& state = *state_;
    const double scale = effective_text_scale();
    const double inset = content_padding * scale;
    const double gap = paragraph_gap * scale;
    Layout result;
    Parts& parts = result.parts;
    double y = 0.0;
    if (!page_.caption.empty()) {
        const double band = caption_height * scale;
        parts.caption_band = PartGeometry::from_bounds({0.0, 0.0, width, band});
        parts.caption = PartGeometry::from_bounds({inset, 0.0, std::max(0.0, width - inset * 2.0), band});
        y = band;
    }
    y += inset;
    const double content_top = y;
    double text_x = inset;
    const bool has_icon = page_.icon != TaskDialogIcon::none || page_.icon_image.value != 0U;
    if (has_icon) {
        parts.icon_image = PartGeometry::from_bounds({inset, y, icon_size * scale, icon_size * scale});
        text_x = inset + (icon_size + icon_gap) * scale;
    }
    const double text_width = std::max(0.0, width - text_x - inset);
    const double unbounded = std::numeric_limits<double>::max();
    if (state.heading_) {
        const double height = (*state.heading_).measure({text_width, unbounded}).height;
        parts.heading = PartGeometry::from_bounds({text_x, y, text_width, height});
        y += height + gap;
    }
    if (state.text_) {
        const double height = (*state.text_).measure({text_width, unbounded}).height;
        parts.text = PartGeometry::from_bounds({text_x, y, text_width, height});
        y += height + gap;
    }
    if (state.input_) {
        parts.input = PartGeometry::from_bounds({text_x, y, text_width, input_height * scale});
        y += input_height * scale + gap;
    }
    if (state.progress_) {
        parts.progress = PartGeometry::from_bounds({text_x, y, text_width, progress_height * scale});
        y += progress_height * scale + gap;
    }
    Rect radio_group{};
    for (std::size_t index = 0; index < state.radios_.size(); ++index) {
        const Rect slot{text_x, y, text_width, radio_height * scale};
        result.radios.push_back(slot);
        radio_group = bounds_union(radio_group, slot);
        y += radio_height * scale;
    }
    if (!state.radios_.empty()) y += gap;
    parts.radio_group = PartGeometry::from_bounds(radio_group);
    if (state.expander_button_) {
        const double row = expander_height * scale;
        const double glyph = expander_glyph_size * scale;
        parts.expander_glyph = PartGeometry::from_bounds(
            {text_x, std::floor(y + (row - glyph) * 0.5), glyph, glyph});
        const double button_x = text_x + glyph + expander_glyph_gap * scale;
        parts.expander_button = PartGeometry::from_bounds(
            {button_x, y, std::max(0.0, text_x + text_width - button_x), row});
        y += row + gap * 0.5;
        if (expanded.read() && state.expanded_text_) {
            const double height = (*state.expanded_text_).measure({text_width, unbounded}).height;
            parts.expanded_text = PartGeometry::from_bounds({text_x, y, text_width, height});
            y += height + gap;
        }
    }
    if (state.verification_) {
        parts.verification = PartGeometry::from_bounds({text_x, y, text_width, verification_height * scale});
        y += verification_height * scale + gap;
    }
    if (has_icon) y = std::max(y, content_top + icon_size * scale + gap);
    y += inset - gap;
    // The button band: buttons right-aligned, the default first in reading
    // order of the page.
    const double band_inset = band_padding * scale;
    const double row = button_height * scale;
    const double band_height = row + band_inset * 2.0;
    parts.button_band = PartGeometry::from_bounds({0.0, y, width, band_height});
    parts.button_divider = PartGeometry::from_bounds({0.0, y, width, 1.0});
    const FontSpec font = text_font(font_);
    std::vector<double> widths(state.buttons_.size(), 0.0);
    double total = 0.0;
    for (std::size_t index = 0; index < state.buttons_.size(); ++index) {
        const ResolvedTextLayout label = resolve_text_layout_utf8(page_.buttons[index].text, font);
        const double measured = label.logical_size.width;
        widths[index] = std::max(minimum_button_width * scale, measured + button_text_padding * scale);
        total += widths[index];
    }
    if (!state.buttons_.empty()) total += button_gap * scale * static_cast<double>(state.buttons_.size() - 1U);
    double x = std::max(inset, width - inset - total);
    Rect button_row{};
    for (std::size_t index = 0; index < state.buttons_.size(); ++index) {
        const Rect slot{x, y + band_inset, widths[index], row};
        result.buttons.push_back(slot);
        button_row = bounds_union(button_row, slot);
        x += widths[index] + button_gap * scale;
    }
    parts.button_row = PartGeometry::from_bounds(button_row);
    y += band_height;
    if (state.footnote_) {
        const double note_inset = footnote_padding * scale;
        double note_x = inset;
        const bool has_note_icon = page_.footnote_icon != TaskDialogIcon::none;
        if (has_note_icon) {
            const double side = footnote_icon_size * scale;
            parts.footnote_icon = PartGeometry::from_bounds({inset, y + note_inset, side, side});
            note_x = inset + side + icon_gap * 0.5 * scale;
        }
        const double note_width = std::max(0.0, width - note_x - inset);
        const double height = (*state.footnote_).measure({note_width, unbounded}).height;
        const double band = std::max(height, has_note_icon ? footnote_icon_size * scale : 0.0) + note_inset * 2.0;
        parts.footnote_band = PartGeometry::from_bounds({0.0, y, width, band});
        parts.footnote = PartGeometry::from_bounds({note_x, y + note_inset, note_width, height});
        y += band;
    }
    const Rect whole{0.0, 0.0, width, y};
    parts.region = PartGeometry::from_bounds(whole);
    parts.face = PartGeometry::from_bounds(whole);
    parts.edge = PartGeometry::from_bounds(whole);
    parts.focus = PartGeometry::from_bounds(whole);
    result.height = y;
    return result;
}

Size TaskDialog::measure(Size available) {
    const double width = preferred_width(available);
    const Layout placed = layout(width);
    const Size result{width, placed.height};
    return result;
}

void TaskDialog::arrange(Rect final_bounds) {
    State& state = *state_;
    arrange_self(final_bounds);
    const Layout placed = layout(final_bounds.width);
    const Parts& parts = placed.parts;
    if (state.heading_) set_child_layout(state.heading_, parts.heading.bounds);
    if (state.text_) set_child_layout(state.text_, parts.text.bounds);
    if (state.input_) set_child_layout(state.input_, parts.input.bounds);
    if (state.progress_) set_child_layout(state.progress_, parts.progress.bounds);
    for (std::size_t index = 0; index < state.radios_.size() && index < placed.radios.size(); ++index) {
        set_child_layout(state.radios_[index], placed.radios[index]);
    }
    if (state.expander_button_) set_child_layout(state.expander_button_, parts.expander_button.bounds);
    if (state.expanded_text_) set_child_layout(state.expanded_text_, parts.expanded_text.bounds);
    if (state.verification_) set_child_layout(state.verification_, parts.verification.bounds);
    for (std::size_t index = 0; index < state.buttons_.size() && index < placed.buttons.size(); ++index) {
        set_child_layout(state.buttons_[index], placed.buttons[index]);
    }
    if (state.footnote_) set_child_layout(state.footnote_, parts.footnote.bounds);
}

// -- paint ---------------------------------------------------------------------

void TaskDialog::paint_icon(Painter& painter, TaskDialogIcon icon, const PartGeometry& place) const {
    // A standard icon is a solid stock icon in the panel's glyph colour for
    // the icon's surface state (pending for a warning, invalid for an
    // error), so dark schemes, high contrast and a theme's replacement
    // follow.
    if (!place.present) return;
    const StandardIcon standard = standard_icon(icon);
    if (standard.name.empty()) return;
    ControlVisualContext context = visual_context();
    if (context.surface != ControlSurfaceState::disabled) context.surface = standard.surface;
    const Color colour = effective_theme().resolve(ControlVisualRole::panel, context).glyph;
    const ImageId image = stock_icon_image(standard.name, StockIconStyle::solid);
    paint_stock_icon(painter, *this, image, place.bounds, colour);
}

void TaskDialog::on_paint(Painter& painter, Rect) {
    const Layout placed = layout(client_rectangle().width);
    const Parts& parts = placed.parts;
    // The face and the caption band are the in-window frames' shared
    // chrome (frame_chrome.hpp), as an MDI document frame's.
    frame_chrome::FrameChromePaint chrome{};
    chrome.font = text_font(font_);
    chrome.face = parts.face.bounds;
    if (parts.caption_band.present) {
        chrome.caption = parts.caption_band.bounds;
        chrome.label = parts.caption.bounds;
        chrome.title = page_.caption;
    }
    frame_chrome::paint_frame_chrome(painter, *this, chrome);
    PartPaint part;
    part.font = text_font(font_);
    part.family = ControlVisualRole::panel;
    if (parts.icon_image.present && page_.icon_image.value != 0U) {
        part.role = PartRole::icon;
        part.bounds = parts.icon_image.bounds;
        part.image = page_.icon_image;
        paint_part(painter, *this, part);
        part.image = {};
    } else {
        paint_icon(painter, page_.icon, parts.icon_image);
    }
    if (parts.expander_glyph.present) {
        part.role = PartRole::disclosure;
        part.bounds = parts.expander_glyph.bounds;
        part.mark = expanded.read() ? PartMark::chevron_down : PartMark::chevron_right;
        if (expanded.read()) part.states.insert(AnatomyState::open);
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
        part.states = {};
    }
    part.role = PartRole::face;
    part.bounds = parts.button_band.bounds;
    paint_part(painter, *this, part);
    part.role = PartRole::divider;
    part.bounds = parts.button_divider.bounds;
    paint_part(painter, *this, part);
    if (parts.footnote_band.present) {
        part.role = PartRole::face;
        part.bounds = parts.footnote_band.bounds;
        paint_part(painter, *this, part);
        paint_icon(painter, page_.footnote_icon, parts.footnote_icon);
    }
}

// -- semantics and anatomy -----------------------------------------------------

SemanticDescriptor TaskDialog::semantic_descriptor() const {
    // A dialog (12.13); while shown, its layer takes every press outside it,
    // so it blocks the rest of its window (modal).
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::dialog;
    descriptor.modal = shown_;
    if (!accessible_name.read().empty()) {
        descriptor.name = accessible_name.read();
    } else if (!page_.caption.empty()) {
        descriptor.name = page_.caption;
    } else {
        descriptor.name = page_.heading;
    }
    descriptor.description = page_.text;
    descriptor.exposed = true;
    return descriptor;
}

namespace {

constexpr std::array<AnatomyState, 1> expander_glyph_states{{AnatomyState::open}};

constexpr std::array<PartField<TaskDialog::Parts>, 22> task_dialog_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::edge},
    {.declaration = {.name = "caption_band", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::caption_band},
    {.declaration = {.name = "caption", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::caption},
    {.declaration = {.name = "icon_image", .role = PartRole::icon, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::icon_image},
    {.declaration = {.name = "heading", .role = PartRole::label, .composed_from = "label", .states = {}},
     .member = &TaskDialog::Parts::heading},
    {.declaration = {.name = "text", .role = PartRole::content, .composed_from = "label", .states = {}},
     .member = &TaskDialog::Parts::text},
    {.declaration = {.name = "input", .role = PartRole::well, .composed_from = "text_box", .states = {}},
     .member = &TaskDialog::Parts::input},
    {.declaration = {.name = "progress", .role = PartRole::track, .composed_from = "progress_bar", .states = {}},
     .member = &TaskDialog::Parts::progress},
    {.declaration = {.name = "radio_group", .role = PartRole::region, .composed_from = "radio_button", .states = {}},
     .member = &TaskDialog::Parts::radio_group},
    {.declaration = {.name = "expander_glyph", .role = PartRole::disclosure, .composed_from = {},
                     .states = expander_glyph_states},
     .member = &TaskDialog::Parts::expander_glyph},
    {.declaration = {.name = "expander_button", .role = PartRole::face, .composed_from = "button", .states = {}},
     .member = &TaskDialog::Parts::expander_button},
    {.declaration = {.name = "expanded_text", .role = PartRole::content, .composed_from = "label", .states = {}},
     .member = &TaskDialog::Parts::expanded_text},
    {.declaration = {.name = "verification", .role = PartRole::indicator, .composed_from = "check_box", .states = {}},
     .member = &TaskDialog::Parts::verification},
    {.declaration = {.name = "button_band", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::button_band},
    {.declaration = {.name = "button_divider", .role = PartRole::divider, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::button_divider},
    {.declaration = {.name = "button_row", .role = PartRole::region, .composed_from = "button", .states = {}},
     .member = &TaskDialog::Parts::button_row},
    {.declaration = {.name = "footnote_band", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::footnote_band},
    {.declaration = {.name = "footnote_icon", .role = PartRole::icon, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::footnote_icon},
    {.declaration = {.name = "footnote", .role = PartRole::content, .composed_from = "label", .states = {}},
     .member = &TaskDialog::Parts::footnote},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &TaskDialog::Parts::focus},
}};

constexpr std::array<PartDeclaration, 22> task_dialog_parts = part_declarations(task_dialog_part_fields);
constexpr std::array<AnatomyState, 5> task_dialog_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ControlAnatomy task_dialog_anatomy{
    .control_id = "task_dialog",
    .parts = task_dialog_parts,
    .states = task_dialog_states,
    .custom_states = {},
    .item_systems = {},
};

// The events TaskDialog adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 6> task_dialog_interactions{{
    {.name = "button_clicked", .payload = "TaskDialogButtonClick", .mutable_payload = true},
    {.name = "closed", .payload = "TaskDialogResult", .mutable_payload = false},
    {.name = "navigated", .payload = "", .mutable_payload = false},
    {.name = "expanded_changed", .payload = "bool", .mutable_payload = false},
    {.name = "verification_changed", .payload = "bool", .mutable_payload = false},
    {.name = "radio_button_changed", .payload = "std::string", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& TaskDialog::anatomy() const noexcept {
    return task_dialog_anatomy;
}

void TaskDialog::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record = layout(client_rectangle().width).parts;
    if (expanded.read()) record.expander_glyph.states.insert(AnatomyState::open);
    write_parts(anatomy(), task_dialog_part_fields, record, geometry);
}

TaskDialog::Parts TaskDialog::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), task_dialog_part_fields, published.parts(), record);
    return record;
}

void TaskDialog::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), task_dialog_interactions.begin(), task_dialog_interactions.end());
}

// property_codemod begin TaskDialog (tools/property_families/task_dialog.json; generated, do not edit)

// property_codemod end TaskDialog

} // namespace gui_forms
