#pragma once

#include "gui_forms/event.hpp"
#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/host/image/host_image.hpp"
#include "gui_forms/events.hpp"
#include "gui_forms/host/types/host_capabilities/host_capabilities.hpp"
#include "gui_forms/host/types/host_lifecycle_phase/host_lifecycle_phase.hpp"
#include "gui_forms/host/types/host_services_snapshot/host_services_snapshot.hpp"
#include "gui_forms/host/types/host_session_snapshot/host_session_snapshot.hpp"
#include "gui_forms/types.hpp"
#include "gui_forms/window/presentation/input_settings.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace gui_forms {

class Window;

// Portable result for a host's non-client drag decision. The retained target
// remains authoritative: an authored drag backdrop is draggable only while it
// is the exact hit-test winner, so an interactive descendant keeps ordinary
// pointer input without a platform-specific exclusion rectangle.
enum class WindowChromeHitRole : std::uint8_t {
    client,
    drag_region,
};

struct WindowChromeHit final {
    WindowChromeHitRole role{WindowChromeHitRole::client};
    std::string target_stable_id;
    std::string matched_drag_region_id;

    [[nodiscard]] bool begins_native_drag() const noexcept {
        return role == WindowChromeHitRole::drag_region;
    }
};

enum class WindowChromeRegionError : std::uint8_t {
    none,
    empty_id,
    duplicate_id,
    unresolved_id,
};

struct WindowChromeRegionValidation final {
    WindowChromeRegionError error{WindowChromeRegionError::none};
    std::size_t index{};
    std::string stable_id;

    [[nodiscard]] bool accepted() const noexcept {
        return error == WindowChromeRegionError::none;
    }
};

[[nodiscard]] WindowChromeRegionValidation validate_window_chrome_drag_regions(
    const Window& window, std::span<const std::string> drag_region_ids);

[[nodiscard]] WindowChromeHit resolve_window_chrome_hit(
    Window& window, Point position,
    std::span<const std::string> drag_region_ids);

struct HostMonitor final {
    std::string id;
    Rect frame;
    Rect work_area;
    double scale{1.0};
    bool primary{};

    friend bool operator==(const HostMonitor& left,
                           const HostMonitor& right) noexcept(noexcept(
        left.id == right.id && left.frame == right.frame &&
        left.work_area == right.work_area && left.scale == right.scale &&
        left.primary == right.primary)) {
        return left.id == right.id && left.frame == right.frame &&
               left.work_area == right.work_area && left.scale == right.scale &&
               left.primary == right.primary;
    }
};

enum class HostServiceError : std::uint8_t {
    none,
    unsupported,
    wrong_thread,
    invalid_argument,
    invalid_utf8,
    too_large,
    modal_limit,
    backend_failure,
    after_shutdown,
};

struct HostServiceStatus final {
    HostServiceError error{HostServiceError::none};

    [[nodiscard]] bool accepted() const noexcept {
        return error == HostServiceError::none;
    }
};

// Semantic one-shot cues. Controls never select native files or platform
// sound identifiers; adapters map these meanings to their local presentation.
enum class HostSoundCue : std::uint8_t {
    notification,
    success,
    warning,
    error,
    operation_complete,
};

struct HostSoundCueRequest final {
    HostSoundCue cue{HostSoundCue::notification};
    double gain{1.0};
    // Caller-supplied monotonic time makes coalescing reproducible headlessly.
    std::uint64_t timestamp_nanoseconds{};
};

struct HostMonitorResult final {
    HostServiceStatus status;
    std::vector<HostMonitor> monitors;
};

struct HostClipboardTextResult final {
    HostServiceStatus status;
    std::string text_utf8;
    std::uint64_t generation{};
    bool has_text{};
};

// A clipboard image read: the image and the clipboard generation it was read
// at, no image at that generation, or a failure. One alternative per state
// (house form for results).
class HostClipboardImageResult final {
public:
    // The clipboard's image, read at a clipboard generation.
    [[nodiscard]] static HostClipboardImageResult image_read(HostImage image, std::uint64_t generation) noexcept {
        HostClipboardImageResult result{ImageRead{std::move(image), generation}};
        return result;
    }
    // The clipboard held no image at this generation.
    [[nodiscard]] static HostClipboardImageResult nothing_read(std::uint64_t generation) noexcept {
        HostClipboardImageResult result{NothingRead{generation}};
        return result;
    }
    // A failed read. `none` is no failure, so it is read as `backend_failure`.
    [[nodiscard]] static HostClipboardImageResult failed(HostServiceError error) noexcept {
        const HostServiceError reason = error == HostServiceError::none ? HostServiceError::backend_failure : error;
        HostClipboardImageResult result{Failed{reason}};
        return result;
    }

    [[nodiscard]] HostServiceStatus status() const noexcept {
        const Failed* const failure = std::get_if<Failed>(&state_);
        const HostServiceStatus status{failure != nullptr ? (*failure).error : HostServiceError::none};
        return status;
    }
    // The clipboard's image; null when the clipboard holds none or the read
    // failed.
    [[nodiscard]] const HostImage* image() const noexcept {
        const ImageRead* const read = std::get_if<ImageRead>(&state_);
        const HostImage* const found = read != nullptr ? &(*read).image : nullptr;
        return found;
    }
    // The clipboard generation read at; 0 for a failed read.
    [[nodiscard]] std::uint64_t generation() const noexcept {
        const ImageRead* const read = std::get_if<ImageRead>(&state_);
        const NothingRead* const empty = std::get_if<NothingRead>(&state_);
        std::uint64_t generation = 0U;
        if (read != nullptr) generation = (*read).generation;
        if (empty != nullptr) generation = (*empty).generation;
        return generation;
    }
    // Moves the image out of a result the caller is finished with
    // (`std::move(result).release_image()`), so a paste keeps the pixels
    // without copying them; empty when the result holds no image.
    [[nodiscard]] std::optional<HostImage> release_image() && noexcept {
        ImageRead* const read = std::get_if<ImageRead>(&state_);
        std::optional<HostImage> released{};
        if (read != nullptr) released.emplace(std::move((*read).image));
        return released;
    }

private:
    struct ImageRead final {
        HostImage image{};
        std::uint64_t generation{};
    };
    struct NothingRead final {
        std::uint64_t generation{};
    };
    struct Failed final {
        HostServiceError error{HostServiceError::backend_failure};
    };
    // A throwing move would leave the variant valueless, a state no query
    // describes.
    static_assert(std::is_nothrow_move_constructible_v<ImageRead>);
    static_assert(std::is_nothrow_move_assignable_v<ImageRead>);

    explicit HostClipboardImageResult(ImageRead read) noexcept : state_(std::move(read)) {}
    explicit HostClipboardImageResult(NothingRead empty) noexcept : state_(empty) {}
    explicit HostClipboardImageResult(Failed failure) noexcept : state_(failure) {}

    std::variant<ImageRead, NothingRead, Failed> state_;
};

// File-manager copies carry local file references as well as optional icon
// bitmaps. Consumers choose their own codecs and must prefer these references
// when their command imports the file's contents.
struct HostClipboardFilesResult final {
    HostServiceStatus status;
    std::vector<std::string> paths_utf8;
    std::uint64_t generation{};
};

// Completion candidates a host may own (Windows Forms' AutoCompleteSource
// values FileSystem to AllSystemSources). No host adapter supplies them yet.
enum class HostAutoCompleteSource : std::uint8_t {
    file_system,
    file_system_directories,
    history_list,
    recently_used_list,
    all_url,
    all_system_sources,
};

// `prefix_utf8` is borrowed for the call. A host returns at most
// `maximum_candidates` strings, each valid UTF-8, in its own order.
struct HostAutoCompleteRequest final {
    HostAutoCompleteSource source{HostAutoCompleteSource::file_system};
    std::string_view prefix_utf8{};
    std::size_t maximum_candidates{256U};
};

struct HostAutoCompleteResult final {
    HostServiceStatus status;
    std::vector<std::string> candidates_utf8;
};

enum class HostDialogOutcome : std::uint8_t {
    accepted,
    cancelled,
};

// A message box's buttons, each set in its order on Windows: OK; OK,
// Cancel; Yes, No; Yes, No, Cancel; Retry, Cancel; Abort, Retry, Ignore;
// Cancel, Try Again, Continue.
enum class HostMessageButtons : std::uint8_t {
    ok,
    ok_cancel,
    yes_no,
    yes_no_cancel,
    retry_cancel,
    abort_retry_ignore,
    cancel_try_continue,
};

enum class HostMessageIcon : std::uint8_t {
    none,
    information,
    warning,
    error,
    question,
};

enum class HostDialogChoice : std::uint8_t {
    none,
    ok,
    cancel,
    yes,
    no,
    retry,
    abort,
    ignore,
    try_again,
    // `continue` is a keyword.
    continue_,
    // A message dialog's Help button, only ever as the default choice of a
    // request with help_button: pressing it asks for help and never closes
    // the dialog, so it is never a result.
    help,
};

struct HostFileDialogFilter final {
    std::string label;
    std::vector<std::string> extensions;

    friend bool operator==(const HostFileDialogFilter& left,
                           const HostFileDialogFilter& right) noexcept(noexcept(
        left.label == right.label && left.extensions == right.extensions)) {
        return left.label == right.label &&
               left.extensions == right.extensions;
    }
};

struct HostMessageDialogRequest final {
    std::string title;
    std::string message;
    HostMessageButtons buttons{HostMessageButtons::ok};
    HostMessageIcon icon{HostMessageIcon::none};
    HostDialogChoice default_choice{HostDialogChoice::ok};
    // Windows Forms' MessageBoxOptions: the text aligned to the right, and
    // read right to left (with the caption and the box mirrored where the
    // host can). Presentation only: an adapter that cannot honour one shows
    // the box as without it (see show_message_box).
    bool right_align{};
    bool rtl_reading{};
    // Windows Forms' displayHelpButton: a Help button after the set's
    // buttons. Pressing it, or F1 where the host's dialog takes it, reports
    // a HostDialogHelpEvent and leaves the dialog open.
    bool help_button{};
};

// The user asked for help in an open message dialog: its Help button, or
// F1. The dialog stays open.
struct HostDialogHelpEvent final {
    std::uint64_t request_id{};
    bool keyboard_initiated{};
};

struct HostOpenFileDialogRequest final {
    std::string title;
    std::string initial_directory;
    std::string suggested_name;
    std::vector<HostFileDialogFilter> filters;
    bool allow_multiple{};
    // The filter the dialog starts with, an index into `filters`; zero when
    // there are none.
    std::size_t initial_filter{};
};

struct HostSaveFileDialogRequest final {
    std::string title;
    std::string initial_directory;
    std::string suggested_name;
    std::string default_extension;
    std::vector<HostFileDialogFilter> filters;
    bool confirm_overwrite{true};
    // The filter the dialog starts with, an index into `filters`; zero when
    // there are none.
    std::size_t initial_filter{};
};

struct HostFolderDialogRequest final {
    std::string title;
    std::string initial_directory;
};

struct HostColorDialogRequest final {
    std::string title;
    std::uint32_t initial_rgba{0x000000FFU};
    bool allow_alpha{};
};

using HostDialogRequestPayload = std::variant<
    HostMessageDialogRequest,
    HostOpenFileDialogRequest,
    HostSaveFileDialogRequest,
    HostFolderDialogRequest,
    HostColorDialogRequest>;

struct HostDialogRequest final {
    std::uint64_t request_id{};
    // Portable stable identity for the logical owner. An empty value means the
    // application's active top-level window; it is never a native handle.
    std::string owner_id;
    HostDialogRequestPayload payload;
};

struct HostMessageDialogResult final {
    HostDialogOutcome outcome{HostDialogOutcome::cancelled};
    HostDialogChoice choice{HostDialogChoice::none};
};

struct HostPathDialogResult final {
    HostDialogOutcome outcome{HostDialogOutcome::cancelled};
    std::vector<std::string> paths;
    // For an accepted file dialog, the request's filter selected when the
    // user accepted, an index into its `filters`; none when the request had
    // no filters, the user chose a filter the host added (such as "All
    // files"), or the host cannot tell.
    std::optional<std::size_t> chosen_filter{};
};

struct HostColorDialogResult final {
    HostDialogOutcome outcome{HostDialogOutcome::cancelled};
    std::uint32_t rgba{};
};

using HostDialogResultPayload = std::variant<
    HostMessageDialogResult,
    HostPathDialogResult,
    HostColorDialogResult>;

struct HostDialogResult final {
    HostServiceStatus status;
    std::uint64_t request_id{};
    HostDialogResultPayload payload;
};

// Notification-area (system tray) icons. An icon is named by the identity
// HostServices::add_tray_icon returns; no native handle enters the portable
// contract. `images` holds the icon at one or more sizes (each at most 256
// pixels a side); an adapter shows the image nearest the size its tray asks
// for (select_tray_icon_image). `tooltip` is at most 127 bytes of UTF-8.
struct HostTrayIconRequest final {
    std::vector<HostImage> images;
    std::string tooltip;
};

// The identity of one notification-area icon (owner decision V3-1): assigned
// by HostServices::add_tray_icon from 1 and never reused by that
// HostServices; zero is no icon. A distinct type, so it cannot be confused
// with a window, runtime or request identity, or with a host's own native
// icon number.
struct HostTrayIconId final {
    std::uint64_t value{};

    [[nodiscard]] bool valid() const noexcept { return value != 0U; }
    friend bool operator==(HostTrayIconId left, HostTrayIconId right) noexcept {
        const bool equal = left.value == right.value;
        return equal;
    }
    friend bool operator!=(HostTrayIconId left, HostTrayIconId right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

struct HostTrayIconResult final {
    HostServiceStatus status;
    HostTrayIconId icon_id{};
};

// What the user did to a tray icon, reported on the UI thread. `context` is
// the platform's context gesture (a secondary click, or the menu key).
enum class HostTrayIconAction : std::uint8_t {
    click,
    double_click,
    context,
    notification_clicked,
    notification_closed,
};

struct HostTrayIconEvent final {
    HostTrayIconId icon_id{};
    HostTrayIconAction action{HostTrayIconAction::click};
};

enum class HostTrayNotificationIcon : std::uint8_t { none, information, warning, error };

// Notification text from a tray icon: a title of at most 63 bytes and text
// of at most 255 bytes of UTF-8, shown for about `timeout_milliseconds` where
// the platform honours a timeout (zero leaves it to the platform).
struct HostTrayNotificationRequest final {
    std::string title;
    std::string text;
    HostTrayNotificationIcon icon{HostTrayNotificationIcon::none};
    std::uint32_t timeout_milliseconds{};
};

// One row of a native tray menu: a command with an identity, or a separator.
struct HostTrayMenuItem final {
    std::string id;
    std::string text;
    bool enabled{true};
    bool checked{};
    bool separator{};
};

struct HostTrayMenuRequest final {
    std::vector<HostTrayMenuItem> items;
};

// The chosen row's identity, or empty when the menu was dismissed.
struct HostTrayMenuResult final {
    HostServiceStatus status;
    std::string chosen_id;
};

// The index of the image to show for a tray that asks for `wanted_side`
// pixels: the smallest image whose larger side reaches it, or the largest
// image when none does. `images` must not be empty.
[[nodiscard]] std::size_t select_tray_icon_image(std::span<const HostImage> images,
                                                 std::uint32_t wanted_side) noexcept;

struct HostModalTransition final {
    std::uint64_t request_id{};
    std::uint32_t depth{};
    bool entering{};
};

enum class HostCloseReason : std::uint8_t {
    user,
    application,
    session_end,
    test,
};

struct HostAttachEvent final {
    Size client_size{};
    double scale{1.0};
};

struct HostResizeEvent final {
    Size client_size{};
};

struct HostScaleEvent final {
    double scale{1.0};
};

struct HostActivationEvent final {
    bool active{};
};

struct HostOcclusionEvent final {
    bool occluded{};
};

struct HostDisplayEvent final {
    std::vector<HostMonitor> monitors;
};

struct HostCloseRequest final {
    HostCloseReason reason{HostCloseReason::user};
    bool cancel{};
    // Adapter policy: an accepted request hides a reusable window while its
    // session remains attached. Observers can cancel; changing this policy
    // during notification does not change the adapter's action.
    bool hide_on_accept{};
};

struct HostClosedEvent final {
    HostCloseReason reason{HostCloseReason::user};
};

struct HostShutdownEvent final {};

// The host's compositor and preference state (stage T5): reported once after
// attach and again whenever it changes (capability compositor_notifications).
// Each report's generation is later than the last; a report whose opaque
// backdrop is not supported is refused as invalid_payload.
struct HostCompositorEvent final {
    CompositorState state{};
};

// The platform's input settings that controls read (stage W, ListView label
// editing): reported after attach and again whenever the system setting
// changes. The session copies them into the window's PresentationSettings
// (double_click_milliseconds, double_click_origin and pointer), leaving
// every other field as it is. An interval of 0 or above
// maximum_double_click_milliseconds, or pointer settings that are not
// valid_pointer_settings, are refused as invalid_payload. A host reports
// the pointer values its platform has with origin host and leaves the
// others at their defaults (origin default_value).
struct HostInputSettingsEvent final {
    std::uint32_t double_click_milliseconds{default_double_click_milliseconds};
    SettingOrigin double_click_origin{SettingOrigin::host};
    PointerSettings pointer{};
};

// The platform asked for a context menu (owner's mouse-event rule): Windows
// WM_CONTEXTMENU (after the right button's release, or Shift+F10 and the
// Menu key), macOS menuForEvent: (the right button's press, or a
// Control-click). The Linux host, whose platform has no such event,
// emulates it: the right button's press, the Menu key and Shift+F10. A
// pointer request carries the pointer's window position; a keyboard
// request names none and goes to the focused control. The window raises
// ContextMenuRequested from it (SurfaceRoot::dispatch_context_request).
// A position that is not finite is refused as invalid_payload.
struct HostContextRequestEvent final {
    ContextRequestOrigin origin{ContextRequestOrigin::pointer};
    Point position{};
};

// The pointer rested within the hover rectangle for the hover time, as the
// platform measured it (Windows WM_MOUSEHOVER). Sent only by a host whose
// input settings say native_hover; the window raises MouseHover on the
// hovered control. A position that is not finite is refused as
// invalid_payload.
struct HostHoverEvent final {
    Point position{};
};

// The native window's state changed (capability window_state_reports;
// owner, 2026-11-08: the window outer band). Reported after attach and
// whenever the window enters or leaves full screen, or is maximized,
// snapped or restored, any of which the platform may answer by reshaping
// its corners. full_screen: the window fills its monitor with no frame
// (Windows: a captionless window covering its monitor; macOS:
// the window style mask's full-screen bit; X11: _NET_WM_STATE_FULLSCREEN, or the
// host's own override-redirect fallback). The session passes full_screen
// to Window::report_full_screen and asks the host for the window's corners
// again (HostServices::apply_window_corners), so the band follows both.
struct HostWindowStateEvent final {
    bool full_screen{};
};

using HostEventPayload = std::variant<
    HostAttachEvent,
    HostResizeEvent,
    HostScaleEvent,
    HostActivationEvent,
    HostOcclusionEvent,
    HostDisplayEvent,
    DragEvent,
    PointerEvent,
    KeyEvent,
    TextInputEvent,
    HostCloseRequest,
    HostClosedEvent,
    HostShutdownEvent,
    HostCompositorEvent,
    HostInputSettingsEvent,
    HostContextRequestEvent,
    HostHoverEvent,
    HostWindowStateEvent>;

struct HostEvent final {
    std::uint64_t sequence{};
    std::uint64_t timestamp_nanoseconds{};
    HostEventPayload payload;
};

enum class HostDispatchError : std::uint8_t {
    none,
    sequence_zero,
    non_monotonic_sequence,
    wrong_thread,
    after_shutdown,
    invalid_lifecycle,
    invalid_geometry,
    invalid_payload,
    callback_fault,
};

// Portable presentation lifecycle. Native adapters may have private allocation
// and handle-binding steps, but no portable callback or input may escape before
// attached. A cancelled close remains attached; an allowed close becomes
// close_authorized and admits only terminal host notifications.
struct HostDispatchResult final {
    bool handled{};
    bool input_deferred{};
    bool input_capacity_rejected{};
    bool close_allowed{true};
    DragEffect drag_effect{DragEffect::none};
    HostDispatchError error{HostDispatchError::none};

    [[nodiscard]] bool accepted() const noexcept {
        return error == HostDispatchError::none;
    }
};

[[nodiscard]] const char* host_dispatch_error_name(HostDispatchError error) noexcept;
[[nodiscard]] const char* host_lifecycle_phase_name(HostLifecyclePhase phase) noexcept;
[[nodiscard]] const char* host_event_name(const HostEventPayload& payload) noexcept;
[[nodiscard]] const char* host_service_error_name(HostServiceError error) noexcept;
[[nodiscard]] const char* cursor_kind_name(CursorKind cursor) noexcept;
[[nodiscard]] const char* drag_effect_name(DragEffect effect) noexcept;
[[nodiscard]] const char* host_dialog_kind_name(
    const HostDialogRequestPayload& payload) noexcept;
[[nodiscard]] const char* host_dialog_outcome_name(HostDialogOutcome outcome) noexcept;
[[nodiscard]] const char* host_dialog_choice_name(HostDialogChoice choice) noexcept;
[[nodiscard]] const char* host_sound_cue_name(HostSoundCue cue) noexcept;
[[nodiscard]] const char* host_tray_icon_action_name(HostTrayIconAction action) noexcept;

} // namespace gui_forms
