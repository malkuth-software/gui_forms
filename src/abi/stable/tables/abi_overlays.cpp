// GF_TABLE_OVERLAYS: overlay roots, anchored overlays (since 1.1) and
// tooltips. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/application.hpp"

#include "gui_forms/components/tool_tip/tool_tip.hpp"
#include "gui_forms/controls/anchored_overlay_layer.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity open_overlay_slot{GF_TABLE_OVERLAYS, 0U};
constexpr SlotIdentity close_overlay_slot{GF_TABLE_OVERLAYS, 1U};
constexpr SlotIdentity set_tool_tip_slot{GF_TABLE_OVERLAYS, 2U};
constexpr SlotIdentity show_tool_tip_slot{GF_TABLE_OVERLAYS, 3U};
constexpr SlotIdentity hide_tool_tip_slot{GF_TABLE_OVERLAYS, 4U};
constexpr SlotIdentity open_anchored_slot{GF_TABLE_OVERLAYS, 5U};
constexpr SlotIdentity close_anchored_slot{GF_TABLE_OVERLAYS, 6U};
constexpr SlotIdentity set_tool_tips_shown_slot{GF_TABLE_OVERLAYS, 7U};
constexpr SlotIdentity tool_tips_shown_slot{GF_TABLE_OVERLAYS, 8U};

// One open overlay: its token and the subscription to the token's closed
// event, which drops the entry when the window closes the overlay (its
// owner left the window, the window closed), so the map holds open
// overlays only (N26).
struct OpenOverlay final {
    OverlayToken token;
    SubscriptionToken closed;
};

// The open overlays, by the overlay's identity. Owned here and used on the UI
// thread only; the rest close when the process ends.
std::unordered_map<gf_object, std::shared_ptr<OpenOverlay>>& open_overlays() {
    static std::unordered_map<gf_object, std::shared_ptr<OpenOverlay>> overlays;
    return overlays;
}

// The token's closed event: the window closed the overlay. Drops the entry
// if it is still this one; the handler holds the entry until it returns,
// so the subscription being emitted is not destroyed under it.
struct OverlayClosedByWindow final {
    gf_object overlay = 0U;
    std::weak_ptr<OpenOverlay> entry;
    void operator()() const {
        const std::shared_ptr<OpenOverlay> held = entry.lock();
        if (!held) {
            return;
        }
        std::unordered_map<gf_object, std::shared_ptr<OpenOverlay>>& overlays = open_overlays();
        const std::unordered_map<gf_object, std::shared_ptr<OpenOverlay>>::iterator found = overlays.find(overlay);
        if (found != overlays.end() && (*found).second == held) {
            overlays.erase(found);
        }
    }
};

gf_status enter_tool_tip(gf_object object, SlotIdentity slot, std::shared_ptr<ToolTip>& tool_tip) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr component;
    const gf_status found = registry().find_component(object, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    std::shared_ptr<ToolTip> cast = std::dynamic_pointer_cast<ToolTip>(component);
    if (!cast) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the object is not a tool_tip component");
    }
    tool_tip = std::move(cast);
    return GF_STATUS_OK;
}

gf_status GF_CALL overlays_open_overlay(gf_object owner, gf_object overlay) noexcept {
    try {
        const ShadowFrameScope shadow_frame(open_overlay_slot.table_id, open_overlay_slot.slot_index);
        Control::Ptr owner_control;
        const gf_status entered = enter_live_control(owner, open_overlay_slot, owner_control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control::Ptr overlay_control;
        const gf_status found = registry().find_object(overlay, open_overlay_slot, overlay_control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        std::unordered_map<gf_object, std::shared_ptr<OpenOverlay>>& overlays = open_overlays();
        const std::unordered_map<gf_object, std::shared_ptr<OpenOverlay>>::iterator existing = overlays.find(overlay);
        if (existing != overlays.end() && (*(*existing).second).token.connected()) {
            return fail(GF_STATUS_INVALID_STATE, open_overlay_slot, "the overlay is already open");
        }
        Window* const window = (*owner_control).attached_window();
        if (window == nullptr) {
            return fail(GF_STATUS_INVALID_STATE, open_overlay_slot, "the owner is not in a window");
        }
        // Window::open_overlay refuses an overlay with a parent and an owner that
        // cannot own one (std::logic_error: GF_STATUS_INVALID_STATE).
        const std::shared_ptr<OpenOverlay> entry = std::make_shared<OpenOverlay>();
        (*entry).token = (*window).open_overlay(owner_control, overlay_control);
        if (Event<>* closed = (*entry).token.closed_event()) {
            (*entry).closed = (*closed).subscribe(OverlayClosedByWindow{overlay, entry});
        }
        overlays.insert_or_assign(overlay, entry);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(open_overlay_slot);
    }
}

gf_status GF_CALL overlays_close_overlay(gf_object overlay) noexcept {
    try {
        const ShadowFrameScope shadow_frame(close_overlay_slot.table_id, close_overlay_slot.slot_index);
        const gf_status entered = registry().enter_ui(close_overlay_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::unordered_map<gf_object, std::shared_ptr<OpenOverlay>>& overlays = open_overlays();
        const std::unordered_map<gf_object, std::shared_ptr<OpenOverlay>>::iterator existing = overlays.find(overlay);
        if (existing == overlays.end()) {
            return fail(GF_STATUS_NOT_FOUND, close_overlay_slot, "the overlay is not open");
        }
        std::shared_ptr<OpenOverlay> held = std::move((*existing).second);
        overlays.erase(existing);
        const bool was_open = (*held).token.connected();
        // Not told of its own close; then dropping the token disconnects it,
        // closing the overlay.
        (*held).closed.disconnect();
        held.reset();
        if (!was_open) {
            return fail(GF_STATUS_NOT_FOUND, close_overlay_slot, "the overlay was already closed by its window");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(close_overlay_slot);
    }
}

gf_status GF_CALL overlays_set_tool_tip(gf_object tool_tip, gf_object target, gf_utf8_view text) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_tool_tip_slot.table_id, set_tool_tip_slot.slot_index);
        std::shared_ptr<ToolTip> provider;
        const gf_status entered = enter_tool_tip(tool_tip, set_tool_tip_slot, provider);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control::Ptr target_control;
        const gf_status found = registry().find_object(target, set_tool_tip_slot, target_control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        std::string_view read;
        const gf_status checked = read_text(text, set_tool_tip_slot, read);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        (*provider).set_tool_tip(target_control, std::string(read));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_tool_tip_slot);
    }
}

gf_status GF_CALL overlays_show_tool_tip(gf_object tool_tip, gf_object target, std::uint64_t duration_ms) noexcept {
    try {
        const ShadowFrameScope shadow_frame(show_tool_tip_slot.table_id, show_tool_tip_slot.slot_index);
        std::shared_ptr<ToolTip> provider;
        const gf_status entered = enter_tool_tip(tool_tip, show_tool_tip_slot, provider);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control::Ptr target_control;
        const gf_status found = registry().find_object(target, show_tool_tip_slot, target_control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        if (duration_ms == 0U) {
            (*provider).show(target_control);
            return GF_STATUS_OK;
        }
        const std::chrono::milliseconds duration{static_cast<std::chrono::milliseconds::rep>(duration_ms)};
        (*provider).show(target_control, duration);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(show_tool_tip_slot);
    }
}

gf_status GF_CALL overlays_hide_tool_tip(gf_object tool_tip) noexcept {
    try {
        const ShadowFrameScope shadow_frame(hide_tool_tip_slot.table_id, hide_tool_tip_slot.slot_index);
        std::shared_ptr<ToolTip> provider;
        const gf_status entered = enter_tool_tip(tool_tip, hide_tool_tip_slot, provider);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*provider).hide();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(hide_tool_tip_slot);
    }
}

// ---------------------------------------------------------------------------
// Anchored overlays (since 1.1): open_anchored and close_anchored.

// The 1.1 size of gf_anchored_overlay_options.
constexpr std::uint32_t anchored_options_size_1_1 = 40U;
static_assert(sizeof(gf_anchored_overlay_options) == anchored_options_size_1_1);

constexpr gf_anchored_overlay_flags known_anchored_flags =
    GF_ANCHORED_OVERLAY_ABOVE | GF_ANCHORED_OVERLAY_ALIGN_FAR | GF_ANCHORED_OVERLAY_NO_FLIP |
    GF_ANCHORED_OVERLAY_KEEP_ON_CLICK_AWAY | GF_ANCHORED_OVERLAY_KEEP_ON_ESCAPE |
    GF_ANCHORED_OVERLAY_TAKE_FOCUS;

// One open anchored overlay: the layer the library made around the
// caller's content, the overlay and focus scope it holds in the anchor's
// window, and the caller's events callback. Owned by anchored_overlays()
// while open; the subscriptions' targets hold it weakly. UI thread only.
struct AnchoredSession final {
    gf_object content = 0U;
    Control::Ptr content_control;
    std::shared_ptr<AnchoredOverlayLayer> layer;
    // The anchor's window, observed while the overlay is connected: the
    // window detaches the overlay (closing its focus scope) before it goes.
    // Borrowed: the sanitizer tree reports a use after the window died.
    BorrowedPointer<Window> window = nullptr;
    OverlayToken token;
    FocusScopeId scope{};
    std::unique_ptr<CallbackBinding> events;
    SubscriptionToken dismiss;
    SubscriptionToken revoked;
    bool closed = false;
    // Invokes of events on the stack (N24): a close from inside one (a
    // menu's callback closing its own content) defers the release until
    // the outermost invoke returns, as the header promises.
    std::uint32_t invoking = 0U;
};

// The open anchored overlays, by their content's identity.
std::unordered_map<gf_object, std::shared_ptr<AnchoredSession>>& anchored_overlays() {
    static std::unordered_map<gf_object, std::shared_ptr<AnchoredSession>> sessions;
    return sessions;
}

// Sends one event to the session's callback with the content as sender.
// Returns the event's cancel field. A session closed during the invoke
// releases its callback here, once no invoke of it is on the stack.
std::uint32_t emit_anchored(AnchoredSession& session, std::string_view name) noexcept {
    if (!session.events) {
        return 0U;
    }
    gf_event event{};
    event.struct_size = sizeof(gf_event);
    event.sender = session.content;
    event.name.data = name.data();
    event.name.size = name.size();
    ++session.invoking;
    (*session.events).invoke(event);
    --session.invoking;
    if (session.closed && session.invoking == 0U) {
        session.events.reset();
    }
    return event.cancel;
}

// Closes the session once: the focus scope (while the overlay is still
// attached), the subscriptions, the overlay, the layer's hold on the
// content; then "closed" and the callback's release. The caller holds a
// reference, so erasing the map's entry does not destroy the session
// under it.
void close_anchored_session(const std::shared_ptr<AnchoredSession>& session) noexcept {
    AnchoredSession& held = *session;
    if (held.closed) {
        return;
    }
    held.closed = true;
    // The token first: a disconnected overlay's window may be gone, and the
    // borrowed pointer must not be read then.
    if (held.token.connected() && held.scope && held.window != nullptr) {
        try {
            static_cast<void>((*held.window).end_focus_scope(held.scope));
        } catch (...) {
        }
    }
    held.scope = {};
    held.dismiss.disconnect();
    held.revoked.disconnect();
    held.token.disconnect();
    held.window = nullptr;
    if (held.layer) {
        try {
            (*held.layer).clear_content();
        } catch (...) {
        }
    }
    static_cast<void>(emit_anchored(held, "closed"));
    // Released now unless an invoke of it is still running (N24); the
    // outermost one releases it as it returns.
    if (held.invoking == 0U) held.events.reset();
    std::unordered_map<gf_object, std::shared_ptr<AnchoredSession>>& sessions = anchored_overlays();
    const std::unordered_map<gf_object, std::shared_ptr<AnchoredSession>>::iterator entry =
        sessions.find(held.content);
    if (entry != sessions.end() && (*entry).second == session) {
        sessions.erase(entry);
    }
}

// The layer's dismiss_requested: the caller may cancel, else it closes.
struct AnchoredDismissRequested final {
    std::weak_ptr<AnchoredSession> session;
    void operator()(OverlayDismissReason reason) const {
        const std::shared_ptr<AnchoredSession> held = session.lock();
        if (!held || (*held).closed) {
            return;
        }
        const std::string_view name = reason == OverlayDismissReason::click_away ? "click_away" : "escape_key";
        const std::uint32_t cancel = emit_anchored(*held, name);
        if (cancel == 0U) {
            close_anchored_session(held);
        }
    }
};

// The overlay's closed event: the window revoked it (the anchor left the
// window, the window closed) or it was disconnected.
struct AnchoredRevoked final {
    std::weak_ptr<AnchoredSession> session;
    void operator()() const {
        const std::shared_ptr<AnchoredSession> held = session.lock();
        if (held) {
            close_anchored_session(held);
        }
    }
};

gf_status open_anchored_overlay(gf_object anchor, gf_object content, const gf_anchored_overlay_options* options) {
    if (options == nullptr || (*options).struct_size < anchored_options_size_1_1 ||
        ((*options).flags & ~known_anchored_flags) != 0U || !std::isfinite((*options).width) ||
        !std::isfinite((*options).height) || !std::isfinite((*options).gap) || (*options).width < 0.0 ||
        (*options).height < 0.0 || (*options).gap < 0.0) {
        return fail(GF_STATUS_INVALID_ARGUMENT, open_anchored_slot,
                    "the options need at least the 1.1 size, known flags and a finite, non-negative size and gap");
    }
    Control::Ptr anchor_control;
    const gf_status entered = enter_live_control(anchor, open_anchored_slot, anchor_control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Control::Ptr content_control;
    const gf_status found = registry().find_object(content, open_anchored_slot, content_control);
    if (found != GF_STATUS_OK) {
        return found;
    }
    if (!(*content_control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, open_anchored_slot, "the content has been disposed");
    }
    if (anchored_overlays().find(content) != anchored_overlays().end()) {
        return fail(GF_STATUS_INVALID_STATE, open_anchored_slot, "the content is already open");
    }
    if ((*content_control).parent() || (*content_control).attached()) {
        return fail(GF_STATUS_INVALID_STATE, open_anchored_slot, "the content has a parent or is in a window");
    }
    Window* const window = (*anchor_control).attached_window();
    if (window == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, open_anchored_slot, "the anchor is not in a window");
    }
    std::unique_ptr<CallbackBinding> events;
    if ((*options).events != nullptr) {
        gf_callback copy{};
        const gf_status read = read_callback((*options).events, open_anchored_slot, copy);
        if (read != GF_STATUS_OK) {
            return read;
        }
        events = std::make_unique<CallbackBinding>(copy);
    }
    const gf_anchored_overlay_flags flags = (*options).flags;
    const Size client = (*window).client_size();
    // A zero extent takes the content's own measure within the window, made
    // once the content is in the window (N25): detached, a control measures
    // text by estimate, not with the window's text engine.
    const Size requested{(*options).width, (*options).height};
    const bool measures = requested.width == 0.0 || requested.height == 0.0;
    Size preferred = requested;
    AnchoredOverlayPlacement placement;
    placement.preferred_size = preferred;
    placement.horizontal_alignment = (flags & GF_ANCHORED_OVERLAY_ALIGN_FAR) != 0U
        ? OverlayHorizontalAlignment::far : OverlayHorizontalAlignment::near;
    placement.vertical_preference = (flags & GF_ANCHORED_OVERLAY_ABOVE) != 0U
        ? OverlayVerticalPreference::above : OverlayVerticalPreference::below;
    placement.gap = (*options).gap;
    placement.allow_vertical_flip = (flags & GF_ANCHORED_OVERLAY_NO_FLIP) == 0U;

    const std::shared_ptr<AnchoredSession> session = std::make_shared<AnchoredSession>();
    AnchoredSession& held = *session;
    held.content = content;
    held.content_control = content_control;
    held.events = std::move(events);
    try {
        const std::string layer_id = std::string((*content_control).stable_id().value()) + ".anchored.layer";
        held.layer = make_control<AnchoredOverlayLayer>(StableId(layer_id), anchor_control, placement);
        (*held.layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
        (*held.layer).dismiss_on_click_away = (flags & GF_ANCHORED_OVERLAY_KEEP_ON_CLICK_AWAY) == 0U;
        (*held.layer).dismiss_on_escape = (flags & GF_ANCHORED_OVERLAY_KEEP_ON_ESCAPE) == 0U;
        (*held.layer).set_content(content_control);
        // Window::open_overlay refuses a disabled or hidden anchor
        // (std::logic_error: GF_STATUS_INVALID_STATE).
        held.token = (*window).open_overlay(anchor_control, held.layer);
        held.window = (*window).presenting_window();
        if (measures) {
            const Size measured = (*content_control).measure(client);
            AnchoredOverlayPlacement attached = placement;
            attached.preferred_size = {requested.width == 0.0 ? measured.width : requested.width,
                                       requested.height == 0.0 ? measured.height : requested.height};
            (*held.layer).placement = attached;
        }
        if ((flags & GF_ANCHORED_OVERLAY_TAKE_FOCUS) != 0U) {
            held.scope = (*window).begin_focus_scope(held.layer, content_control);
        }
        held.dismiss = (*held.layer).dismiss_requested().subscribe(AnchoredDismissRequested{session});
        if (Event<>* closed = held.token.closed_event()) {
            held.revoked = (*closed).subscribe(AnchoredRevoked{session});
        }
        anchored_overlays().insert_or_assign(content, session);
    } catch (...) {
        // Nothing stays open, and the caller keeps its callback.
        if (held.events) (*held.events).disarm();
        held.events.reset();
        held.closed = true;
        if (held.scope && held.token.connected()) {
            static_cast<void>((*window).end_focus_scope(held.scope));
        }
        held.dismiss.disconnect();
        held.revoked.disconnect();
        held.token.disconnect();
        if (held.layer) (*held.layer).clear_content();
        anchored_overlays().erase(content);
        throw;
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL overlays_open_anchored(gf_object anchor, gf_object content,
                                         const gf_anchored_overlay_options* options) noexcept {
    try {
        const ShadowFrameScope shadow_frame(open_anchored_slot.table_id, open_anchored_slot.slot_index);
        const gf_status opened = open_anchored_overlay(anchor, content, options);
        return opened;
    } catch (...) {
        return fail_with_current_exception(open_anchored_slot);
    }
}

gf_status GF_CALL overlays_close_anchored(gf_object content) noexcept {
    try {
        const ShadowFrameScope shadow_frame(close_anchored_slot.table_id, close_anchored_slot.slot_index);
        const gf_status entered = registry().enter_ui(close_anchored_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::unordered_map<gf_object, std::shared_ptr<AnchoredSession>>& sessions = anchored_overlays();
        const std::unordered_map<gf_object, std::shared_ptr<AnchoredSession>>::iterator entry =
            sessions.find(content);
        if (entry == sessions.end()) {
            return fail(GF_STATUS_NOT_FOUND, close_anchored_slot, "the content is not open");
        }
        const std::shared_ptr<AnchoredSession> session = (*entry).second;
        close_anchored_session(session);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(close_anchored_slot);
    }
}

// ---------------------------------------------------------------------------
// Tooltip switches (since 1.1): a window's, or the program's.

// The live window `object` names, through its handle; null with a status
// when it is not a window or has closed.
gf_status find_tool_tips_window(gf_object object, SlotIdentity slot, Window*& window) {
    Component::Ptr component;
    const gf_status found = registry().find_component(object, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(component);
    if (handle == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the object is not a window");
    }
    Window* const held = (*handle).window();
    if (!(*handle).is_alive() || held == nullptr) {
        return fail(GF_STATUS_DISPOSED, slot, "the window has been disposed or has closed");
    }
    window = held;
    return GF_STATUS_OK;
}

gf_status GF_CALL overlays_set_tool_tips_shown(gf_object window, gf_tool_tips_setting setting) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_tool_tips_shown_slot.table_id, set_tool_tips_shown_slot.slot_index);
        const gf_status entered = registry().enter_ui(set_tool_tips_shown_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (setting != GF_TOOL_TIPS_FOLLOW && setting != GF_TOOL_TIPS_SHOWN && setting != GF_TOOL_TIPS_HIDDEN) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_tool_tips_shown_slot, "unknown tooltip setting");
        }
        if (window == 0U) {
            if (setting == GF_TOOL_TIPS_FOLLOW) {
                return fail(GF_STATUS_INVALID_ARGUMENT, set_tool_tips_shown_slot,
                            "the program's switch is shown or hidden");
            }
            Application* const application = Application::current();
            if (application == nullptr) {
                return fail(GF_STATUS_INVALID_STATE, set_tool_tips_shown_slot, "no application is current");
            }
            (*application).set_tool_tips_shown(setting == GF_TOOL_TIPS_SHOWN);
            return GF_STATUS_OK;
        }
        Window* target = nullptr;
        const gf_status found = find_tool_tips_window(window, set_tool_tips_shown_slot, target);
        if (found != GF_STATUS_OK) {
            return found;
        }
        std::optional<bool> shown{};
        if (setting != GF_TOOL_TIPS_FOLLOW) shown = setting == GF_TOOL_TIPS_SHOWN;
        (*target).set_tool_tips_shown(shown);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_tool_tips_shown_slot);
    }
}

gf_status GF_CALL overlays_tool_tips_shown(gf_object window, std::uint32_t* shown,
                                           gf_tool_tips_setting* setting) noexcept {
    try {
        const ShadowFrameScope shadow_frame(tool_tips_shown_slot.table_id, tool_tips_shown_slot.slot_index);
        if (shown == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, tool_tips_shown_slot, "tool_tips_shown needs an output");
        }
        const gf_status entered = registry().enter_ui(tool_tips_shown_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (window == 0U) {
            const Application* const application = Application::current();
            *shown = application == nullptr || (*application).tool_tips_shown() ? 1U : 0U;
            if (setting != nullptr) *setting = GF_TOOL_TIPS_FOLLOW;
            return GF_STATUS_OK;
        }
        Window* target = nullptr;
        const gf_status found = find_tool_tips_window(window, tool_tips_shown_slot, target);
        if (found != GF_STATUS_OK) {
            return found;
        }
        *shown = (*target).tool_tips_shown() ? 1U : 0U;
        if (setting != nullptr) {
            const std::optional<bool> own = (*target).tool_tips_shown_setting();
            *setting = !own.has_value() ? GF_TOOL_TIPS_FOLLOW : (*own ? GF_TOOL_TIPS_SHOWN : GF_TOOL_TIPS_HIDDEN);
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(tool_tips_shown_slot);
    }
}

constexpr gf_overlays_table table{
    .struct_size = sizeof(gf_overlays_table),
    .abi_minor = GF_ABI_MINOR,
    .open_overlay = &overlays_open_overlay,
    .close_overlay = &overlays_close_overlay,
    .set_tool_tip = &overlays_set_tool_tip,
    .show_tool_tip = &overlays_show_tool_tip,
    .hide_tool_tip = &overlays_hide_tool_tip,
    .open_anchored = &overlays_open_anchored,
    .close_anchored = &overlays_close_anchored,
    .set_tool_tips_shown = &overlays_set_tool_tips_shown,
    .tool_tips_shown = &overlays_tool_tips_shown,
};

} // namespace

const gf_overlays_table& overlays_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
