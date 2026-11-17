// Native popups, phase (a) (owner, 2026-11-04): the portable seam on the
// headless reference host. A popup is an undecorated native window owned
// by the window that opens it, placed at the owner's client origin plus
// its bounds and kept inside the work area; it closes with its token, with
// the control that opened it, or when the host destroys it. A run that
// cannot open one falls back to an overlay, or refuses when asked to.
#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/font_dialog.hpp"
#include "gui_forms/controls/task_dialog.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/window/native_popup/native_popup.hpp"
#include "application/headless_application.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <stxlib/optional.hpp>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool contains(const std::string& text, const char* fragment) {
    const bool found = text.find(fragment) != std::string::npos;
    return found;
}

struct KeepReady final {
    ApplicationWindowHandle* handle{};
    Window** model{};
    void operator()(Window& window, ApplicationWindowHandle value) const {
        *handle = value;
        *model = &window;
    }
};

std::shared_ptr<Control> make_content(const char* id) {
    std::shared_ptr<Control> content = make_control<Control>(StableId(std::string_view(id)));
    return content;
}

// The primary window "main", 800 x 600 at 100,100 on the headless work
// area (0,0 1920 x 1040), with one child control that opens popups.
struct Desk final {
    ApplicationWindowHandle main{};
    Window* model{};
    std::shared_ptr<Control> opener{};
    std::unique_ptr<host::HeadlessApplication> application{};

    explicit Desk(HostCapabilities capabilities = host::headless_capabilities()) {
        std::vector<ApplicationWindow> windows;
        ApplicationWindow primary{};
        primary.stable_id = "main";
        std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
        opener = make_control<Control>(StableId("opener"));
        (*opener).set_requested_bounds(Rect{10.0, 10.0, 80.0, 24.0});
        (*root).add_child(opener);
        primary.model = std::make_unique<Window>(root, Size{800.0, 600.0});
        primary.options.title = "main";
        primary.options.initial_size = {800.0, 600.0};
        primary.options.ready = KeepReady{&main, &model};
        windows.push_back(std::move(primary));
        application = std::make_unique<host::HeadlessApplication>(std::move(windows), capabilities);
        require(model != nullptr && main.active(), "the main window is ready");
    }
    host::HeadlessWindowing& windowing() { return (*application).windowing(); }
};

void opens_a_native_window() {
    Desk desk;
    (*desk.application).windowing().clear_trace();
    NativePopupToken token = (*desk.model).open_native_popup(desk.opener, make_content("popup.content"),
                                                             Rect{20.0, 40.0, 200.0, 120.0}, NativePopupOptions{});
    require(token.surface() == NativePopupSurface::native && token.connected(), "the popup is native");
    require(token.refusal() == NativePopupRefusal::none, "nothing refused it");
    require(token.id().valid() && token.window() != nullptr, "the token names the popup window");
    require(desk.windowing().window_count() == 2U, "the popup is a window of the run");
    require(desk.windowing().kind(token.id()) == stxlib::Optional<HostWindowKind>::of(HostWindowKind::popup), "its kind is popup");
    require(desk.windowing().owner(token.id()) == stxlib::Optional<HostWindowId>::of(desk.main.id()), "the window that opened it owns it");
    const std::string& trace = desk.windowing().trace();
    require(contains(trace, "kind=popup") && contains(trace, "focus=passive"), "the host traces the popup");
    const std::optional<Rect> content = desk.windowing().native_content(token.id());
    require(content.has_value() && *content == Rect{120.0, 140.0, 200.0, 120.0},
            "it is placed at the owner's client origin plus its bounds");
    const HostWindowFrame frame = desk.windowing().native_frame(token.id());
    require(frame.outer == frame.content, "a popup has no decorations");
    require((*token.window()).client_size() == Size{200.0, 120.0}, "its model has the bounds' size");
    require((*token.window()).backdrop_request().preferred() == WindowBackdrop::transparent,
            "without a chain the popup asks for the transparent backdrop");
    token.disconnect();
    require(!token.connected() && desk.windowing().window_count() == 1U, "disconnecting closes it");
    require(token.window() == nullptr && !token.id().valid(), "a closed popup names nothing");
}

void keeps_inside_the_work_area() {
    Desk desk;
    NativePopupToken token = (*desk.model).open_native_popup(desk.opener, make_content("popup.edge"),
                                                             Rect{1700.0, 900.0, 300.0, 200.0}, NativePopupOptions{});
    const std::optional<Rect> content = desk.windowing().native_content(token.id());
    require(content.has_value() && *content == Rect{1620.0, 840.0, 300.0, 200.0},
            "a popup past the work area's edge moves the least distance back inside");
}

void takes_focus_is_declared() {
    Desk desk;
    NativePopupOptions options{};
    options.focus = HostPopupFocus::takes_focus;
    desk.windowing().clear_trace();
    NativePopupToken token = (*desk.model).open_native_popup(desk.opener, make_content("popup.focus"),
                                                             Rect{0.0, 0.0, 100.0, 50.0}, options);
    require(contains(desk.windowing().trace(), "focus=takes_focus"), "the declared focus reaches the host");
}

struct CountClosed final {
    int* count{};
    void operator()() const { *count = *count + 1; }
};

void closes_with_its_opener_and_its_host() {
    Desk desk;
    NativePopupToken first = (*desk.model).open_native_popup(desk.opener, make_content("popup.a"),
                                                             Rect{0.0, 0.0, 100.0, 50.0}, NativePopupOptions{});
    static_cast<void>((*(*desk.model).root()).remove_child((*desk.opener).runtime_id()));
    desk.opener.reset();
    require(!first.connected() && desk.windowing().window_count() == 1U,
            "the popup closes when the control that opened it is gone");
    std::shared_ptr<Control> other = make_control<Control>(StableId("other"));
    (*(*desk.model).root()).add_child(other);
    NativePopupToken second = (*desk.model).open_native_popup(other, make_content("popup.b"),
                                                              Rect{0.0, 0.0, 100.0, 50.0}, NativePopupOptions{});
    const HostWindowId id = second.id();
    int closed = 0;
    require(second.closed_event() != nullptr, "a native popup has a closed event");
    SubscriptionToken heard = (*second.closed_event()).subscribe(CountClosed{&closed});
    require(desk.windowing().destroy_window(id).accepted(), "the host may destroy the popup window");
    require(!second.connected(), "the token sees the host's close");
    require(closed == 1, "the closed event is raised once for the host's close");
    second.disconnect();
}

void falls_back_without_native_popups() {
    // Without runtime windows the run cannot open a popup window.
    HostCapabilities limited = host::headless_capabilities();
    using CapabilityBits = std::underlying_type_t<HostCapability>;
    const CapabilityBits kept = static_cast<CapabilityBits>(limited.available) &
                                static_cast<CapabilityBits>(~static_cast<CapabilityBits>(HostCapability::runtime_windows));
    limited.available = static_cast<HostCapability>(kept);
    Desk desk(limited);
    NativePopupToken overlay = (*desk.model).open_native_popup(desk.opener, make_content("popup.overlay"),
                                                               Rect{0.0, 0.0, 100.0, 50.0}, NativePopupOptions{});
    require(overlay.surface() == NativePopupSurface::overlay && overlay.connected(),
            "the content opens as an overlay");
    require(overlay.refusal() == NativePopupRefusal::unsupported, "the refusal names the host");
    require(desk.windowing().window_count() == 1U && overlay.window() == nullptr, "no window opened");
    overlay.disconnect();
    require(!overlay.connected(), "disconnecting closes the overlay");
    NativePopupOptions refuse{};
    refuse.fallback = NativePopupFallback::refuse;
    NativePopupToken none = (*desk.model).open_native_popup(desk.opener, make_content("popup.none"),
                                                            Rect{0.0, 0.0, 100.0, 50.0}, refuse);
    require(none.surface() == NativePopupSurface::none && !none.connected(), "or nothing opens");
    // A window outside any run has no native popups either.
    std::shared_ptr<Control> root = make_control<Control>(StableId("loose.root"));
    std::shared_ptr<Control> opener = make_control<Control>(StableId("loose.opener"));
    (*opener).set_requested_bounds(Rect{0.0, 0.0, 20.0, 20.0});
    (*root).add_child(opener);
    Window loose(root, Size{200.0, 100.0});
    NativePopupToken loose_token =
        loose.open_native_popup(opener, make_content("loose.popup"), Rect{0.0, 0.0, 50.0, 20.0}, NativePopupOptions{});
    require(loose_token.surface() == NativePopupSurface::overlay, "a window outside a run uses an overlay");
    require(loose_token.refusal() == NativePopupRefusal::no_run, "the refusal names the missing run");
}

void entries_are_validated() {
    ApplicationWindow orphan{};
    orphan.stable_id = "orphan";
    orphan.model = std::make_unique<Window>(make_content("orphan.root"), Size{100.0, 50.0});
    orphan.options.initial_size = {100.0, 50.0};
    orphan.options.minimum_size = {1.0, 1.0};
    orphan.popup.set(HostPopupPlacement{});
    std::vector<ApplicationWindow> windows;
    windows.push_back(std::move(orphan));
    require(!Application::validate(windows).accepted(), "a popup needs an owner");
    require(host_window_kind(true, false, HostWindowFloating::none, true) == HostWindowKind::popup,
            "a popup entry is a popup window");
    require(host_window_kind_traits(HostWindowKind::popup).popup &&
                !host_window_kind_traits(HostWindowKind::popup).floating,
            "a popup is not floating");
    Desk desk;
    bool refused = false;
    try {
        static_cast<void>((*desk.model).open_native_popup(desk.opener, make_content("popup.empty"),
                                                          Rect{0.0, 0.0, 0.0, 10.0}, NativePopupOptions{}));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "an empty popup is refused");
}

// Phase (c): an owned dialog asked for a native popup puts its panel in a
// popup window and keeps its modal layer in the owner's window; closing
// detaches it at once, so it can be shown again in the same turn.
struct KeepTaskResult final {
    std::string* button{};
    void operator()(const TaskDialogResult& result) const { *button = result.button_id; }
};

void task_dialog_opens_natively() {
    Desk desk;
    std::shared_ptr<TaskDialog> dialog = make_control<TaskDialog>(StableId("native.task"));
    TaskDialogPage page;
    page.heading = "Native";
    page.allow_cancel = true;
    (*dialog).set_page(std::move(page));
    (*dialog).set_surface(DialogSurface::native_popup);
    std::string button;
    SubscriptionToken kept = (*dialog).closed().subscribe(KeepTaskResult{&button});
    require((*dialog).show(desk.opener), "the dialog shows");
    require((*dialog).presented_surface() == NativePopupSurface::native, "its panel is in a popup window");
    require(desk.windowing().window_count() == 2U, "the popup window is open");
    require((*desk.model).focus_scope_depth() == 1U, "the owner's window keeps the modal layer's scope");
    (*dialog).close("ok");
    require(button == "ok" && desk.windowing().window_count() == 1U, "closing closes the popup window");
    require((*dialog).attached_surface_root() == nullptr && !(*dialog).parent(), "the dialog is detached at once");
    require((*desk.model).focus_scope_depth() == 0U, "the owner's scope ends");
    require((*dialog).show(desk.opener) && (*dialog).presented_surface() == NativePopupSurface::native,
            "it shows again natively");
    // The host closing the popup window cancels the dialog.
    const std::size_t popups = desk.windowing().window_count();
    require(popups == 2U, "the second popup window is open");
    button.clear();
    const std::vector<HostWindowId> ids = desk.windowing().z_order();
    HostWindowId popup{};
    for (std::size_t index = 0U; index < ids.size(); ++index) {
        if (desk.windowing().kind(ids[index]) == stxlib::Optional<HostWindowKind>::of(HostWindowKind::popup)) popup = ids[index];
    }
    require(desk.windowing().destroy_window(popup).accepted(), "the host destroys the popup window");
    require(!(*dialog).shown() && !button.empty(), "the dialog closes as cancelled");
    require(desk.windowing().window_count() == 1U, "no popup window is left");
}

void task_dialog_falls_back_to_an_overlay() {
    HostCapabilities limited = host::headless_capabilities();
    using CapabilityBits = std::underlying_type_t<HostCapability>;
    const CapabilityBits kept = static_cast<CapabilityBits>(limited.available) &
                                static_cast<CapabilityBits>(~static_cast<CapabilityBits>(HostCapability::runtime_windows));
    limited.available = static_cast<HostCapability>(kept);
    Desk desk(limited);
    std::shared_ptr<TaskDialog> dialog = make_control<TaskDialog>(StableId("overlay.task"));
    (*dialog).set_surface(DialogSurface::native_popup);
    require((*dialog).show(desk.opener), "the dialog shows");
    require((*dialog).presented_surface() == NativePopupSurface::overlay, "as an overlay without popup windows");
    require(desk.windowing().window_count() == 1U, "no popup window opened");
    (*dialog).close("ok");
    require((*dialog).presented_surface() == NativePopupSurface::none, "and closes");
}

struct KeepFontResult final {
    DialogResult* result{};
    void operator()(const FontDialogResult& closing) const { *result = closing.result; }
};

void font_dialog_opens_natively() {
    Desk desk;
    std::shared_ptr<FontDialog> dialog = make_control<FontDialog>(StableId("native.font"));
    (*dialog).set_surface(DialogSurface::native_popup);
    DialogResult result{DialogResult::none};
    SubscriptionToken kept = (*dialog).closed().subscribe(KeepFontResult{&result});
    require((*dialog).show(desk.opener), "the font dialog opens");
    require((*dialog).presented_surface() == NativePopupSurface::native, "in a popup window");
    require(desk.windowing().window_count() == 2U, "the popup window is open");
    (*dialog).close(DialogResult::cancel);
    require(result == DialogResult::cancel && desk.windowing().window_count() == 1U, "closing closes it");
    require((*dialog).attached_surface_root() == nullptr, "the font dialog is detached at once");
    require((*dialog).show(desk.opener) && (*dialog).presented_surface() == NativePopupSurface::native,
            "it opens again natively");
    (*dialog).close(DialogResult::ok);
    require(result == DialogResult::ok, "OK closes it");
}

} // namespace

// Plan Paint's Settings dialog, reported 2026-11-13 to 2026-11-17: open
// Settings, choose another Canvas surround, press OK or Cancel; the dialog
// does not close. The dialog is an owned window run by show_modal, its
// surround a ComboBox; OK, Cancel and Escape all end in destroy_window on
// the dialog's window (Plan Paint's DestroyDialogWindow, posted to the
// editor). The drop-down lifted its list into the dialog window's warm
// picker Popup, which stays open, hidden, for the window's life, and
// HostWindowing::destroy_window refused a window that owned any live
// window: the destroy was refused and the nested loop never ended.
struct SettingsTurn final {
    host::HeadlessApplication* application{};
    ApplicationWindowHandle* dialog{};
    Window** model{};
    std::shared_ptr<ComboBox>* surround{};
    // Observations, owned by the test.
    int* turns{};
    std::size_t* windows_while_dropped{};
    HostServiceStatus* destroyed{};
    bool operator()(HostWindowId) const {
        *turns = *turns + 1;
        if (*turns == 1) {
            // The user drops the surround list and picks another felt.
            (*(*model)).perform_layout();
            (*(*surround)).dropped_down = true;
            *windows_while_dropped = (*application).windowing().window_count();
            (*(*surround)).selected_index = stxlib::Optional<std::size_t>::of(2U);
            (*(*surround)).dropped_down = false;
            return true;
        }
        if (*turns == 2) {
            // OK or Cancel: the dialog's window is destroyed.
            *destroyed = (*dialog).windows().destroy_window(*dialog);
            return true;
        }
        // The loop should have ended at turn 2; stop it instead of running
        // its whole bound.
        return false;
    }
};

ApplicationWindow settings_entry(std::shared_ptr<ComboBox>& surround, ApplicationWindowHandle& dialog,
                                 Window*& model) {
    std::shared_ptr<Control> root = make_control<Control>(StableId("editor-dialog"));
    surround = make_control<ComboBox>(StableId("settings-background"));
    const char* const surrounds[] = {"Pale felt", "Brown felt", "Tan felt", "Grey felt", "Plain"};
    for (const char* surround_name : surrounds) (*surround).items.append(surround_name);
    (*surround).selected_index = stxlib::Optional<std::size_t>::of(0U);
    (*surround).set_requested_bounds(Rect{230.0, 60.0, 230.0, 30.0});
    (*root).add_child(surround);
    ApplicationWindow entry{};
    entry.stable_id = "editor-dialog-1";
    entry.owner_id = "main";
    entry.model = std::make_unique<Window>(root, Size{770.0, 474.0});
    entry.options.title = "Settings";
    entry.options.initial_size = {770.0, 474.0};
    entry.options.minimum_size = {770.0, 474.0};
    entry.options.ready = KeepReady{&dialog, &model};
    return entry;
}

void a_window_whose_drop_down_was_used_closes() {
    // Modal, as Plan Paint runs its dialogs.
    {
        Desk desk;
        std::shared_ptr<ComboBox> surround;
        ApplicationWindowHandle dialog{};
        Window* model = nullptr;
        int turns = 0;
        std::size_t windows_while_dropped = 0U;
        HostServiceStatus destroyed{HostServiceError::unsupported};
        desk.windowing().set_modal_turn(SettingsTurn{desk.application.get(), &dialog, &model, &surround, &turns,
                                                     &windows_while_dropped, &destroyed});
        const ApplicationModalResult shown = desk.main.windows().show_modal(settings_entry(surround, dialog, model));
        require(shown.status.accepted(), "the Settings dialog runs modally");
        require(windows_while_dropped == 3U, "the surround list shows in a Popup window the dialog owns");
        require(destroyed.accepted(), "OK or Cancel destroys the dialog's window after a surround was chosen");
        require(turns == 2, "the modal loop ends with the dialog's window");
        require(desk.windowing().window_count() == 1U, "the dialog's window and its Popup are gone");
        (*desk.application).reclaim();
    }
    // Modeless, the same destroy.
    {
        Desk desk;
        std::shared_ptr<ComboBox> surround;
        ApplicationWindowHandle dialog{};
        Window* model = nullptr;
        const ApplicationWindowOpenResult opened = desk.main.windows().open_window(settings_entry(surround, dialog, model));
        require(opened.status.accepted() && model != nullptr, "the Settings dialog opens as an owned window");
        (*model).perform_layout();
        (*surround).dropped_down = true;
        require(desk.windowing().window_count() == 3U, "the modeless list shows in a Popup window");
        (*surround).dropped_down = false;
        require(desk.main.windows().destroy_window(opened.window).accepted(),
                "a window whose drop-down was used is destroyable");
        require(desk.windowing().window_count() == 1U, "its warm Popup went with it");
        // A window the program opened over another still refuses its
        // owner's destroy.
        ApplicationWindow owner_entry{};
        owner_entry.stable_id = "owner";
        owner_entry.owner_id = "main";
        owner_entry.model = std::make_unique<Window>(make_content("owner.root"), Size{200.0, 100.0});
        owner_entry.options.initial_size = {200.0, 100.0};
        const ApplicationWindowOpenResult owner = desk.main.windows().open_window(std::move(owner_entry));
        ApplicationWindow child_entry{};
        child_entry.stable_id = "child";
        child_entry.owner_id = "owner";
        child_entry.model = std::make_unique<Window>(make_content("child.root"), Size{100.0, 80.0});
        child_entry.options.initial_size = {100.0, 80.0};
        const ApplicationWindowOpenResult child = desk.main.windows().open_window(std::move(child_entry));
        require(owner.status.accepted() && child.status.accepted(), "the owner and its child open");
        require(desk.main.windows().destroy_window(owner.window).error == HostServiceError::invalid_argument,
                "a window that owns a program window still refuses the destroy");
        require(desk.main.windows().destroy_window(child.window).accepted() &&
                    desk.main.windows().destroy_window(owner.window).accepted(),
                "child first, then its owner");
    }
}

int main() {
    try {
        opens_a_native_window();
        keeps_inside_the_work_area();
        takes_focus_is_declared();
        closes_with_its_opener_and_its_host();
        falls_back_without_native_popups();
        entries_are_validated();
        task_dialog_opens_natively();
        task_dialog_falls_back_to_an_overlay();
        font_dialog_opens_natively();
        a_window_whose_drop_down_was_used_closes();
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_native_popup_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "gui_forms_native_popup_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
