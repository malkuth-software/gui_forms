#include "gui_forms/controls/task_dialog.hpp"
#include "../frame_chrome/frame_chrome.hpp"
#include "../dialog_presentation/provisional_child.hpp"

#include "gui_forms/controls/progress_bar.hpp"
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <stxlib/optional.hpp>
#include <unordered_set>
#include <utility>

namespace gui_forms {
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
    presentation_.abandon();
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
    if (page.progress_bar.present()) {
        const TaskDialogProgressBar& progress = (*page.progress_bar.value());
        if (!std::isfinite(progress.minimum) || !std::isfinite(progress.maximum) ||
            !std::isfinite(progress.value) || progress.minimum >= progress.maximum) {
            throw std::invalid_argument("TaskDialog progress bounds must be finite and ordered");
        }
    }
    // The page and its child controls change together (state hunt C21): a
    // child the window refuses part-way (an identity another control of
    // the window holds) brings back the page shown before, with its
    // children, and the refusal leaves. Should that rebuild be refused too,
    // the dialog is left with an empty page and no children, which agree.
    TaskDialogPage previous = std::move(page_);
    page_ = std::move(page);
    try {
        rebuild_children();
    } catch (...) {
        page_ = std::move(previous);
        try {
            rebuild_children();
        } catch (...) {
            page_ = TaskDialogPage{};
            clear_children_of_page();
            try {
                clear_children();
            } catch (...) {
            }
        }
        throw;
    }
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if (!presentation_.shown()) return;
    // A page shown in place of another: focus moves to its default button.
    Window* const host = attached_window();
    const std::shared_ptr<Button> preferred = button(default_button_id());
    if (host != nullptr && preferred) static_cast<void>((*host).request_focus(preferred));
    navigated_.emit();
}

void TaskDialog::clear_children_of_page() noexcept {
    child_tokens_.clear();
    buttons_.clear();
    radios_.clear();
    heading_.reset();
    text_.reset();
    expanded_text_.reset();
    footnote_.reset();
    verification_.reset();
    expander_button_.reset();
    progress_.reset();
}

std::shared_ptr<Label> TaskDialog::make_paragraph(std::string_view suffix, const std::string& text,
                                                  FontSpec font) {
    std::string identity(stable_id().value());
    identity.append(suffix);
    std::shared_ptr<Label> paragraph = make_control<Label>(StableId(std::move(identity)), text);
    (*paragraph).font = stxlib::Optional<FontSpec>::of(font);
    (*paragraph).text_wrapping = TextWrapping::word;
    (*paragraph).vertical_alignment = VerticalAlignment::start;
    add_child(paragraph);
    return paragraph;
}

void TaskDialog::rebuild_children() {
    clear_children_of_page();
    clear_children();
    const std::string prefix(stable_id().value());
    FontSpec heading_font = font_;
    heading_font.size = heading_size;
    heading_font.weight = 600;
    FontSpec footnote_font = font_;
    footnote_font.size = footnote_size;
    if (!page_.heading.empty()) heading_ = make_paragraph(".heading", page_.heading, heading_font);
    if (!page_.text.empty()) text_ = make_paragraph(".text", page_.text, font_);
    if (page_.progress_bar.present()) {
        const TaskDialogProgressBar& settings = (*page_.progress_bar.value());
        progress_ = make_control<ProgressBar>(StableId(prefix + ".progress"));
        (*progress_).set_range(settings.minimum, settings.maximum);
        (*progress_).value = std::clamp(settings.value, settings.minimum, settings.maximum);
        if (settings.marquee) (*progress_).visual_style = ProgressBarVisualStyle::marquee;
        add_child(progress_);
    }
    for (const TaskDialogRadioButton& radio : page_.radio_buttons) {
        std::shared_ptr<RadioButton> control =
            make_control<RadioButton>(StableId(prefix + ".radio." + radio.stable_id), radio.text);
        (*control).checked = radio.checked;
        (*control).enabled = radio.enabled;
        add_child(control);
        child_tokens_.push_back((*control).checked_changed().subscribe(
            *this, Delegate<bool>::bind<TaskDialog, &TaskDialog::on_radio>(*this)));
        radios_.push_back(std::move(control));
    }
    if (page_.expander) {
        const TaskDialogExpander& expander = *page_.expander;
        expander_button_ = make_control<Button>(
            StableId(prefix + ".expander"),
            expander.expanded ? expander.expanded_button_text : expander.collapsed_button_text);
        (*expander_button_).visual_style = ButtonVisualStyle::flat;
        add_child(expander_button_);
        child_tokens_.push_back((*expander_button_).clicked().subscribe(
            *this, Delegate<ButtonBase&>::bind<TaskDialog, &TaskDialog::on_expander>(*this)));
        expanded_text_ = make_paragraph(".expanded-text", expander.text, font_);
        set_child_layout_collapsed(expanded_text_, !expander.expanded);
    }
    if (!page_.verification_text.empty()) {
        verification_ = make_control<CheckBox>(StableId(prefix + ".verification"), page_.verification_text);
        (*verification_).checked = page_.verification_checked;
        add_child(verification_);
        child_tokens_.push_back((*verification_).checked_changed().subscribe(
            *this, Delegate<bool>::bind<TaskDialog, &TaskDialog::on_verification>(*this)));
    }
    const std::string preferred = default_button_id();
    for (const TaskDialogButton& definition : page_.buttons) {
        std::shared_ptr<Button> control =
            make_control<Button>(StableId(prefix + ".button." + std::string(button_segment(definition.stable_id))),
                                 definition.text);
        (*control).enabled = definition.enabled;
        if (definition.stable_id == preferred) (*control).visual_style = ButtonVisualStyle::accent;
        add_child(control);
        child_tokens_.push_back((*control).clicked().subscribe(
            *this, Delegate<ButtonBase&>::bind<TaskDialog, &TaskDialog::on_child_button>(*this)));
        buttons_.push_back(std::move(control));
    }
    if (!page_.footnote.empty()) footnote_ = make_paragraph(".footnote", page_.footnote, footnote_font);
}

stxlib::Optional<std::size_t> TaskDialog::button_index(std::string_view button_id) const noexcept {
    for (std::size_t index = 0; index < page_.buttons.size(); ++index) {
        if (page_.buttons[index].stable_id == button_id) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

std::string TaskDialog::default_button_id() const {
    if (!page_.default_button.empty()) return page_.default_button;
    if (page_.buttons.empty()) return {};
    return page_.buttons.front().stable_id;
}

std::string TaskDialog::cancel_button_id() const {
    for (const TaskDialogButton& definition : page_.buttons) {
        if (definition.standard == TaskDialogStandardButton::cancel) return definition.stable_id;
    }
    return "cancel";
}

std::shared_ptr<Button> TaskDialog::button(std::string_view button_id) const {
    const stxlib::Optional<std::size_t> index = button_index(button_id);
    if (index.absent() || (*index.value()) >= buttons_.size()) return nullptr;
    return buttons_[(*index.value())];
}

std::shared_ptr<RadioButton> TaskDialog::radio_button(std::string_view radio_id) const {
    for (std::size_t index = 0; index < page_.radio_buttons.size() && index < radios_.size(); ++index) {
        if (page_.radio_buttons[index].stable_id == radio_id) return radios_[index];
    }
    return nullptr;
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
    require_mutable();
    if (!page_.expander || (*page_.expander).expanded == incoming) return;
    TaskDialogExpander& expander = *page_.expander;
    expander.expanded = incoming;
    if (expander_button_) {
        (*expander_button_).text = incoming ? expander.expanded_button_text : expander.collapsed_button_text;
    }
    if (expanded_text_) set_child_layout_collapsed(expanded_text_, !incoming);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    // The layer centres the dialog again at its new height.
    const Control::Ptr layer = presentation_.layer();
    if (layer) (*layer).invalidate(Dirty::arrange);
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
    if (verification_) {
        (*verification_).checked = incoming;
        return;
    }
    page_.verification_checked = incoming;
}

void TaskDialog::set_progress_value(double value) {
    require_mutable();
    if (page_.progress_bar.absent() || !progress_ || !std::isfinite(value)) return;
    TaskDialogProgressBar& settings = (*page_.progress_bar.value());
    settings.value = std::clamp(value, settings.minimum, settings.maximum);
    (*progress_).value = settings.value;
}

// -- interaction ---------------------------------------------------------------

void TaskDialog::on_child_button(ButtonBase& sender) {
    for (std::size_t index = 0; index < buttons_.size(); ++index) {
        if (buttons_[index].get() == &sender) {
            const std::string identity = page_.buttons[index].stable_id;
            static_cast<void>(click(identity));
            return;
        }
    }
}

bool TaskDialog::click(std::string_view button_id) {
    require_mutable();
    const stxlib::Optional<std::size_t> index = button_index(button_id);
    if (index.absent() || !page_.buttons[(*index.value())].enabled) return false;
    TaskDialogButtonClick request{std::string(button_id), !page_.buttons[(*index.value())].allow_close};
    // A handler may drop the dialog's last other reference; it stays alive
    // for the checks below (A-gf-controls-13-F6).
    const Control::Ptr lifetime = weak_from_this().lock();
    button_clicked_.emit(request);
    // A handler may have navigated to another page or closed the dialog.
    if (!is_alive() || request.keep_open || !presentation_.shown()) return true;
    finish(request.button_id);
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
    if (synchronizing_) return;
    std::string newly_checked{};
    for (std::size_t index = 0; index < radios_.size() && index < page_.radio_buttons.size(); ++index) {
        const bool checked = (*radios_[index]).checked.read();
        if (checked && !page_.radio_buttons[index].checked) newly_checked = page_.radio_buttons[index].stable_id;
        page_.radio_buttons[index].checked = checked;
    }
    if (!newly_checked.empty()) radio_button_changed_.emit(newly_checked);
}

void TaskDialog::on_key_preview(KeyEvent& event) {
    if (!presentation_.shown() || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::escape) {
        const std::string cancel = cancel_button_id();
        if (button_index(cancel).present()) {
            static_cast<void>(click(cancel));
        } else if (page_.allow_cancel) {
            finish(cancel);
        }
        event.handled = true;
        return;
    }
    if (event.physical_key != PhysicalKey::enter) return;
    // A focused button takes its own Enter; elsewhere Enter is the default
    // button's.
    Window* const host = attached_window();
    const Control::Ptr focused = host != nullptr ? (*host).focused_control() : nullptr;
    for (const std::shared_ptr<Button>& candidate : buttons_) {
        if (focused == candidate) return;
    }
    if (focused && focused == expander_button_) return;
    static_cast<void>(click(default_button_id()));
    event.handled = true;
}

// -- showing -------------------------------------------------------------------

bool TaskDialog::show(const Control::Ptr& owner) {
    require_mutable();
    Window* const window = owner ? (*owner).attached_window() : nullptr;
    if (presentation_.shown() || window == nullptr || parent()) return false;
    Window& host = *window;
    // A page never set has no buttons; setting it gives it the OK button.
    if (page_.buttons.empty()) set_page(page_);
    const Size client = host.client_size();
    // `lifetime` holds the dialog across the handlers the steps below run.
    const Control::Ptr lifetime = shared_from_this();
    Control::Ptr layer = make_control<TaskDialogLayer>(StableId(std::string(stable_id().value()) + ".layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    // Until the presentation is installed, every step is undone on an
    // exception: the guard takes the dialog out of the layer, and the
    // owner half's lease and subscription end with it.
    ProvisionalChild in_layer(layer, lifetime);
    PresentationLayer owner_half{};
    owner_half.overlay = host.open_overlay(owner, layer);
    owner_half.layer = layer;
    if (Event<>* revoked = owner_half.overlay.closed_event()) {
        owner_half.overlay_closed =
            (*revoked).subscribe(*this, Delegate<>::bind<TaskDialog, &TaskDialog::on_overlay_revoked>(*this));
    }
    DialogPresentation shown = surface_ == DialogSurface::native_popup
        ? present_natively(host, owner, std::move(owner_half))
        : DialogPresentation::overlay(std::move(owner_half));
    in_layer.commit();
    presentation_ = std::move(shown);
    result_.reset();
    // begin_focus_scope moves the focus, running the previous control's
    // focus and validation handlers; one may close the dialog (and even
    // show it again), and then the scope this show() opened is ended here,
    // since finish found none to end (A-gf-controls-13-F4).
    // The default button takes focus when it can; a disabled or hidden one
    // is not offered to the window, which would refuse it and leave the
    // dialog shown with no scope (state hunt, run 6). Any other refusal (a
    // containing scope, the nesting limit) takes the dialog down again
    // before it leaves, with nothing raised.
    Control::Ptr preferred = button(default_button_id());
    if (preferred && !((*preferred).eligible_for_input() && (*preferred).focusable.read())) preferred = {};
    FocusScopeId started{};
    try {
        if (Window* const popup = presentation_.popup_window()) {
            static_cast<void>((*popup).begin_focus_scope(presentation_.popup_root(), preferred));
        }
        if (!is_alive() || !presentation_.shown_with(*layer)) return true;
        if (presentation_.surface() == NativePopupSurface::native) {
            // The empty layer keeps the owner's keys from its controls.
            started = host.begin_focus_scope(layer);
        } else {
            started = host.begin_focus_scope(layer, preferred);
        }
    } catch (...) {
        if (is_alive() && presentation_.shown_with(*layer)) abandon_show();
        throw;
    }
    if (!is_alive() || !presentation_.adopt_scope(*layer, started)) {
        if (started) static_cast<void>(host.end_focus_scope(started));
        return true;
    }
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
    return true;
}

DialogPresentation TaskDialog::present_natively(Window& host, const Control::Ptr& owner,
                                                PresentationLayer owner_half) {
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
    static_cast<void>((*owner_half.layer).remove_child(runtime_id()));
    PresentationPopup popup_half{};
    popup_half.root = make_control<Control>(StableId(std::string(stable_id().value()) + ".native"));
    set_requested_bounds({0.0, 0.0, width, height});
    // Taken out of the root again if the popup does not open, or throws.
    ProvisionalChild in_root(popup_half.root, self);
    NativePopupOptions options{};
    options.focus = HostPopupFocus::takes_focus;
    options.fallback = NativePopupFallback::refuse;
    popup_half.popup = host.open_native_popup(owner, popup_half.root, bounds, options);
    if (popup_half.popup.surface() != NativePopupSurface::native) {
        in_root.undo();
        (*owner_half.layer).add_child(self);
        DialogPresentation fallback = DialogPresentation::overlay(std::move(owner_half));
        return fallback;
    }
    if (Event<>* closed = popup_half.popup.closed_event()) {
        popup_half.popup_closed =
            (*closed).subscribe(*this, Delegate<>::bind<TaskDialog, &TaskDialog::on_overlay_revoked>(*this));
    }
    in_root.commit();
    DialogPresentation shown = DialogPresentation::native(std::move(owner_half), std::move(popup_half));
    return shown;
}

NativePopupSurface TaskDialog::presented_surface() const noexcept {
    const NativePopupSurface result = presentation_.surface();
    return result;
}

void TaskDialog::close(std::string_view button_id) {
    require_mutable();
    finish(std::string(button_id));
}

void TaskDialog::on_overlay_revoked() {
    // The owner left its window: the dialog closes as cancelled.
    finish(cancel_button_id());
}

bool TaskDialog::agrees_with_itself() const noexcept {
    // The page and the child controls it describes, matched by index.
    if (page_.buttons.size() != buttons_.size() || page_.radio_buttons.size() != radios_.size()) return false;
    if (static_cast<bool>(page_.expander) != static_cast<bool>(expander_button_)) return false;
    if (page_.verification_text.empty() == static_cast<bool>(verification_)) return false;
    for (const std::shared_ptr<Button>& control : buttons_) {
        if (!control || (*control).parent().get() != this) return false;
    }
    // Shown: in the presentation's layer or popup root; closed: no parent.
    if (!presentation_.shown()) return !parent();
    const Control::Ptr holder = parent();
    return holder && (holder == presentation_.layer() || holder == presentation_.popup_root());
}

// A show whose scope was refused: the dialog leaves its layer and popup and
// both close, with closed not raised (no show was reported either).
void TaskDialog::abandon_show() noexcept {
    const Control::Ptr retained = weak_from_this().lock();
    DialogPresentation closing = presentation_.take();
    try {
        closing.dismantle(*this, FocusScopeCloseReason::explicit_close);
    } catch (...) {
        closing.abandon();
    }
}

void TaskDialog::finish(std::string button_id) {
    if (!presentation_.shown()) return;
    // Closed from here on, for every handler the steps below run.
    DialogPresentation closing = presentation_.take();
    TaskDialogResult outcome{std::move(button_id), page_.verification_checked, checked_radio_button()};
    closing.dismantle(*this, FocusScopeCloseReason::explicit_close);
    result_ = outcome;
    closed_.emit(outcome);
}

void TaskDialog::on_dispose() noexcept {
    // A disposed dialog closes without raising closed.
    DialogPresentation closing = presentation_.take();
    closing.dismantle_on_dispose();
    child_tokens_.clear();
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
    if (heading_) {
        const double height = (*heading_).measure({text_width, unbounded}).height;
        parts.heading = PartGeometry::from_bounds({text_x, y, text_width, height});
        y += height + gap;
    }
    if (text_) {
        const double height = (*text_).measure({text_width, unbounded}).height;
        parts.text = PartGeometry::from_bounds({text_x, y, text_width, height});
        y += height + gap;
    }
    if (progress_) {
        parts.progress = PartGeometry::from_bounds({text_x, y, text_width, progress_height * scale});
        y += progress_height * scale + gap;
    }
    Rect radio_group{};
    for (std::size_t index = 0; index < radios_.size(); ++index) {
        const Rect slot{text_x, y, text_width, radio_height * scale};
        result.radios.push_back(slot);
        radio_group = bounds_union(radio_group, slot);
        y += radio_height * scale;
    }
    if (!radios_.empty()) y += gap;
    parts.radio_group = PartGeometry::from_bounds(radio_group);
    if (expander_button_) {
        const double row = expander_height * scale;
        const double glyph = expander_glyph_size * scale;
        parts.expander_glyph = PartGeometry::from_bounds(
            {text_x, std::floor(y + (row - glyph) * 0.5), glyph, glyph});
        const double button_x = text_x + glyph + expander_glyph_gap * scale;
        parts.expander_button = PartGeometry::from_bounds(
            {button_x, y, std::max(0.0, text_x + text_width - button_x), row});
        y += row + gap * 0.5;
        if (expanded.read() && expanded_text_) {
            const double height = (*expanded_text_).measure({text_width, unbounded}).height;
            parts.expanded_text = PartGeometry::from_bounds({text_x, y, text_width, height});
            y += height + gap;
        }
    }
    if (verification_) {
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
    std::vector<double> widths(buttons_.size(), 0.0);
    double total = 0.0;
    for (std::size_t index = 0; index < buttons_.size(); ++index) {
        const ResolvedTextLayout label = resolve_text_layout_utf8(page_.buttons[index].text, font);
        const double measured = label.logical_size.width;
        widths[index] = std::max(minimum_button_width * scale, measured + button_text_padding * scale);
        total += widths[index];
    }
    if (!buttons_.empty()) total += button_gap * scale * static_cast<double>(buttons_.size() - 1U);
    double x = std::max(inset, width - inset - total);
    Rect button_row{};
    for (std::size_t index = 0; index < buttons_.size(); ++index) {
        const Rect slot{x, y + band_inset, widths[index], row};
        result.buttons.push_back(slot);
        button_row = bounds_union(button_row, slot);
        x += widths[index] + button_gap * scale;
    }
    parts.button_row = PartGeometry::from_bounds(button_row);
    y += band_height;
    if (footnote_) {
        const double note_inset = footnote_padding * scale;
        double note_x = inset;
        const bool has_note_icon = page_.footnote_icon != TaskDialogIcon::none;
        if (has_note_icon) {
            const double side = footnote_icon_size * scale;
            parts.footnote_icon = PartGeometry::from_bounds({inset, y + note_inset, side, side});
            note_x = inset + side + icon_gap * 0.5 * scale;
        }
        const double note_width = std::max(0.0, width - note_x - inset);
        const double height = (*footnote_).measure({note_width, unbounded}).height;
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
    arrange_self(final_bounds);
    const Layout placed = layout(final_bounds.width);
    const Parts& parts = placed.parts;
    if (heading_) set_child_layout(heading_, parts.heading.bounds);
    if (text_) set_child_layout(text_, parts.text.bounds);
    if (progress_) set_child_layout(progress_, parts.progress.bounds);
    for (std::size_t index = 0; index < radios_.size() && index < placed.radios.size(); ++index) {
        set_child_layout(radios_[index], placed.radios[index]);
    }
    if (expander_button_) set_child_layout(expander_button_, parts.expander_button.bounds);
    if (expanded_text_) set_child_layout(expanded_text_, parts.expanded_text.bounds);
    if (verification_) set_child_layout(verification_, parts.verification.bounds);
    for (std::size_t index = 0; index < buttons_.size() && index < placed.buttons.size(); ++index) {
        set_child_layout(buttons_[index], placed.buttons[index]);
    }
    if (footnote_) set_child_layout(footnote_, parts.footnote.bounds);
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
    descriptor.modal = presentation_.shown();
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

constexpr std::array<PartField<TaskDialog::Parts>, 21> task_dialog_part_fields{{
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

constexpr std::array<PartDeclaration, 21> task_dialog_parts = part_declarations(task_dialog_part_fields);
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
