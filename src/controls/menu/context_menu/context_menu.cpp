#include "gui_forms/components/context_menu/context_menu.hpp"

#include "../menu_utilities.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gui_forms {

using namespace menu_detail;

namespace {

// The one character a text event types, or none when it types several, is
// malformed or is still composing.
[[nodiscard]] std::optional<char32_t> typed_character(
    const TextInputEvent& event) noexcept {
    if (event.composing || event.text_utf8.empty()) return std::nullopt;
    const Utf8ValidationResult validation = validate_utf8(event.text_utf8);
    if (!validation.valid() || validation.scalar_count != 1U) return std::nullopt;
    const std::string_view text = event.text_utf8;
    const char32_t lead = static_cast<unsigned char>(text[0]);
    char32_t scalar = lead;
    if (text.size() == 2U) scalar = lead & 0x1FU;
    else if (text.size() == 3U) scalar = lead & 0x0FU;
    else if (text.size() == 4U) scalar = lead & 0x07U;
    for (std::size_t offset = 1U; offset < text.size(); ++offset) {
        scalar = (scalar << 6U) |
                 (static_cast<char32_t>(static_cast<unsigned char>(text[offset])) & 0x3FU);
    }
    return scalar;
}

} // namespace

struct ContextMenu::Impl final {
    class MenuLayer;
    class MenuPanel;
    class PopupFrame;
    class MenuRow;

    struct PanelState final {
        std::shared_ptr<MenuPanel> panel;
        // The panel's face in the owner's client coordinates, wherever it
        // is shown.
        Rect face{};
        // Shown in the owner's warm Popup window of the panel's depth
        // (frame holds it there), else on the overlay layer.
        std::shared_ptr<PopupFrame> frame;
        bool in_popup{};
        std::vector<std::shared_ptr<MenuRow>> rows;
        const std::vector<MenuSnapshot>* items{};
        std::optional<std::size_t> parent_index;
        std::optional<std::size_t> child_source_index;
        double scroll_offset{};
        double content_height{};
        double viewport_height{};
        double width{};
    };

    explicit Impl(ContextMenu& public_owner) : owner(public_owner) {}

    [[nodiscard]] double text_scale() const noexcept {
        return window ? (*window).presentation_settings().text_scale : 1.0;
    }

    class MenuLayer final : public Panel {
    public:
        MenuLayer(StableId id, Impl& impl) : Panel(std::move(id)), impl_(impl) {
            set_paint_plane(PaintPlane::overlay);
            border_style = BorderStyle::none;
            set_background(Color::rgba(0, 0, 0, 0));
        }

        void on_pointer(PointerEvent& event) override {
            if (impl_.outside_pointer_handler &&
                impl_.outside_pointer_handler(event)) {
                event.handled = true;
                return;
            }
            if (event.phase == EventPhase::target &&
                event.action == PointerAction::down &&
                event.button != PointerButton::none) {
                impl_.close();
                event.handled = true;
            }
        }

        // While the panels show in Popup windows the layer holds the
        // owner's focus (a Popup never takes it) and passes keys and text
        // to the Popup whose panel has the menu's focus.
        void on_key(KeyEvent& event) override {
            if (event.phase != EventPhase::target) return;
            if (impl_.forward_key(event)) event.handled = true;
        }

        void on_text_input(TextInputEvent& event) override {
            if (impl_.forward_text(event)) event.handled = true;
        }

        [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
            SemanticDescriptor descriptor;
            descriptor.exposed = false;
            descriptor.include_descendants = true;
            return descriptor;
        }

    private:
        Impl& impl_;
    };

    class MenuPanel final : public Panel {
    public:
        MenuPanel(StableId id, Impl& impl, std::size_t depth)
            : Panel(std::move(id)), impl_(impl), depth_(depth) {
            set_focusable(true);
            set_paint_plane(PaintPlane::overlay);
            set_background(Color::rgba(250, 253, 255));
            border_style = BorderStyle::line;
        }

        // The panel overlaps the window's content, so it casts the theme's
        // overlay shadow (owner decision RB-37) from its square face before
        // painting the face.
        void on_paint(Painter& painter, Rect local_damage) override {
            if (shadow_) {
                PartPaint shadow;
                shadow.role = PartRole::shadow;
                shadow.family = ControlVisualRole::panel;
                shadow.bounds = local_bounds();
                shadow.corner_radius = 0.0;
                paint_part(painter, *this, shadow);
            }
            Panel::on_paint(painter, local_damage);
        }

        // Whether the panel casts its shadow (true until changed): an opaque
        // Popup window holds the face alone (owner, 2026-11-08: Q-S2 (b)).
        void set_shadow(const bool cast) noexcept {
            if (shadow_ == cast) return;
            shadow_ = cast;
            invalidate(Dirty::layout | Dirty::paint);
        }

        [[nodiscard]] Insets visual_outsets() const noexcept override {
            const Insets panel = Panel::visual_outsets();
            if (!shadow_) return panel;
            const Insets shadow = overlay_shadow_outsets(*this);
            const Insets result{std::max(panel.left, shadow.left), std::max(panel.top, shadow.top),
                                std::max(panel.right, shadow.right), std::max(panel.bottom, shadow.bottom)};
            return result;
        }

        void on_pointer(PointerEvent& event) override {
            if (event.action == PointerAction::wheel && event.wheel_delta.y != 0.0) {
                impl_.scroll(depth_, event.wheel_delta.y > 0.0 ? -3 : 3);
                event.handled = true;
            } else if (event.action == PointerAction::down &&
                       event.button != PointerButton::none) {
                event.handled = true;
            }
        }

        void on_pointer_bubble(PointerEvent& event) override {
            if (!event.handled && event.action == PointerAction::wheel &&
                event.wheel_delta.y != 0.0) {
                impl_.scroll(depth_, event.wheel_delta.y > 0.0 ? -3 : 3);
                event.handled = true;
            }
        }

        void on_key(KeyEvent& event) override {
            if (event.phase != EventPhase::target || event.action != KeyAction::down) return;
            if (event.physical_key == PhysicalKey::down || event.physical_key == PhysicalKey::home) {
                impl_.focus_edge(depth_, false);
            } else if (event.physical_key == PhysicalKey::up || event.physical_key == PhysicalKey::end) {
                impl_.focus_edge(depth_, true);
            } else if (event.physical_key == PhysicalKey::left || event.physical_key == PhysicalKey::right) {
                static_cast<void>(impl_.navigate_root(event.physical_key == PhysicalKey::left ? -1 : 1));
            } else if (event.physical_key == PhysicalKey::escape) {
                impl_.close();
            } else {
                return;
            }
            event.handled = true;
        }

        // A plain letter in an open menu acts as that item's mnemonic, as in
        // a native Windows menu; Alt is not needed.
        void on_text_input(TextInputEvent& event) override {
            const std::optional<char32_t> character = typed_character(event);
            if (character && impl_.type_mnemonic(depth_, std::nullopt, *character)) {
                event.handled = true;
            }
        }

        [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
            SemanticDescriptor descriptor;
            descriptor.role = SemanticRole::menu;
            descriptor.name = "menu";
            descriptor.exposed = true;
            descriptor.include_descendants = true;
            return descriptor;
        }

    private:
        Impl& impl_;
        std::size_t depth_{};
        bool shadow_{true};
    };

    // The client of one Popup window: holds its panel at the panel's face,
    // inset by the shadow's outsets, and reveals it when shown (the pop and
    // slide: the face grows from pop_start of its height to all of it while
    // sliding slide_distance toward its final place, over reveal_duration;
    // reduced motion shows it at once). Kept per depth for the menu's life.
    class PopupFrame final : public Control {
    public:
        static constexpr double pop_start = 0.85;
        static constexpr double slide_distance = 6.0;
        static constexpr FrameInterval reveal_duration = std::chrono::milliseconds(120);

        explicit PopupFrame(StableId id)
            : Control(std::move(id)),
              reveal_(*this, MotionCurve::timed(reveal_duration, MotionEasing::smoothstep), 1.0) {
            set_paint_plane(PaintPlane::overlay);
            reveal_.set_listener(MotionListener::bind<PopupFrame, &PopupFrame::on_reveal>(*this));
        }

        // Makes `panel` the frame's one child, at `face` (frame coordinates)
        // once revealed.
        void hold(const std::shared_ptr<MenuPanel>& panel, Rect face) {
            release();
            panel_ = panel;
            face_ = face;
            (*this).add_child(panel);
            reveal_.jump_to(1.0);
            place();
        }

        // Starts the reveal; `upward` slides from below (a panel placed
        // above its anchor). The frame must show in its window, whose clock
        // moves it; a detached frame shows the panel at once.
        void reveal(bool upward) {
            upward_ = upward;
            reveal_.jump_to(0.0);
            if (reveal_.animate_to(1.0) != MotionStart::started) reveal_.jump_to(1.0);
            place();
        }

        // Takes the panel out, leaving the frame empty.
        void release() noexcept {
            reveal_.finish();
            const std::shared_ptr<MenuPanel> panel = std::move(panel_);
            panel_.reset();
            if (panel && (*panel).parent().get() == this) {
                try {
                    static_cast<void>((*this).remove_child((*panel).runtime_id()));
                } catch (...) {
                }
            }
        }

        [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
            SemanticDescriptor descriptor;
            descriptor.exposed = false;
            descriptor.include_descendants = true;
            return descriptor;
        }

    private:
        void on_reveal(const MotionStep& step) {
            static_cast<void>(step);
            place();
        }

        void place() {
            if (!panel_) return;
            const double progress = std::clamp(reveal_.current(), 0.0, 1.0);
            const double height = face_.height * (pop_start + (1.0 - pop_start) * progress);
            const double slide = slide_distance * (1.0 - progress);
            const double y = upward_ ? face_.y + face_.height - height + slide : face_.y - slide;
            (*panel_).set_requested_bounds({face_.x, y, face_.width, height});
        }

        MotionValue reveal_;
        std::shared_ptr<MenuPanel> panel_{};
        Rect face_{};
        bool upward_{};
    };

    class MenuRow final : public Control {
    public:
        MenuRow(StableId id, Impl& impl, std::size_t depth, std::size_t index,
                const MenuSnapshot& item)
            : Control(std::move(id)), impl_(impl), depth_(depth), index_(index),
              item_(item) {
            set_paint_plane(PaintPlane::overlay);
            enabled = item.kind != MenuItemKind::separator && enabled_item();
            set_focusable(item.kind != MenuItemKind::separator && enabled_item());
            cursor = item.kind != MenuItemKind::separator && enabled_item()
                ? std::optional<CursorKind>{CursorKind::hand}
                : std::optional<CursorKind>{};
        }

        [[nodiscard]] bool enabled_item() const noexcept {
            return snapshot_enabled(item_);
        }

        void set_hot(bool hot) {
            if (hot_ == hot) return;
            hot_ = hot;
            invalidate(Dirty::paint);
        }

        void on_paint(Painter& painter, Rect) override {
            const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                              committed_arranged_bounds().height};
            const BasicControlStyle style;
            // The separator rule, the hot plate, the check or radio mark,
            // the label and the submenu chevron are menu item parts the
            // theme's engine draws; a row is hot while hot or focused and
            // disabled with its item.
            AnatomyStateSet states{};
            if (hot_ || focused_) states.insert(AnatomyState::hot);
            if (!enabled_item()) states.insert(AnatomyState::disabled);
            if (item_.kind == MenuItemKind::separator) {
                PartPaint rule;
                rule.family = ControlVisualRole::menu_item;
                rule.role = PartRole::divider;
                rule.bounds = {30.0, bounds.height * .5 - 0.5,
                               std::max(1.0, bounds.width - 37.0), 1.0};
                paint_part(painter, *this, rule);
                return;
            }
            if (hot_ || focused_) {
                PartPaint plate;
                plate.family = ControlVisualRole::menu_item;
                plate.role = PartRole::face;
                plate.bounds = {2.0, 1.0, std::max(0.0, bounds.width - 4.0),
                                std::max(0.0, bounds.height - 2.0)};
                plate.states = states;
                paint_part(painter, *this, plate);
            }
            if ((item_.kind == MenuItemKind::check ||
                 item_.kind == MenuItemKind::radio) && item_.command_state.checked) {
                PartPaint mark;
                mark.family = ControlVisualRole::menu_item;
                mark.role = PartRole::indicator;
                mark.states = states;
                mark.states.insert(AnatomyState::checked);
                if (item_.kind == MenuItemKind::check) {
                    mark.mark = PartMark::check;
                    mark.bounds = {7.0, 6.0, 14.0, 14.0};
                } else {
                    mark.mark = PartMark::radio_dot;
                    mark.bounds = {9.0, 8.0, 12.0, 12.0};
                }
                paint_part(painter, *this, mark);
            }
            const FontSpec font = effective_font({FontRole::control, 11.0,
                                static_cast<std::uint16_t>(
                                    item_.command_state.default_action ? 700 : 400),
                                item_.italic, 0.12});
            const double baseline = std::max(font.size,
                                              bounds.height * 0.5 + font.size * 0.35);
            const std::string display_text = menu_display_text(item_.text);
            PartPaint label;
            label.family = ControlVisualRole::menu_item;
            label.role = PartRole::label;
            label.states = states;
            label.text = display_text;
            label.font = font;
            // A destructive command's red ink, provisionally an authored
            // colour until the theme has a text tone for it (asked).
            if (item_.command_state.destructive) label.authored_foreground = menu_ink(item_, style);
            paint_text_part_at(painter, *this, label, {30.0, baseline},
                               std::max(0.0, bounds.width - 30.0));
            if (!item_.command_state.shortcut.empty()) {
                const FontSpec shortcut_font = effective_font(
                    {FontRole::content, 10.0, 400, false});
                const Size size = painter.measure_text_utf8(
                    item_.command_state.shortcut, shortcut_font);
                PartPaint shortcut;
                shortcut.family = ControlVisualRole::menu_item;
                shortcut.role = PartRole::label;
                shortcut.tone = PartTextTone::secondary;
                shortcut.states = states;
                shortcut.text = item_.command_state.shortcut;
                shortcut.font = shortcut_font;
                shortcut.measured_text = size;
                paint_text_part_at(painter, *this, shortcut,
                                   {std::max(34.0, bounds.width - size.width - 25.0), baseline},
                                   size.width);
            }
            if (item_.kind == MenuItemKind::submenu) {
                PartPaint chevron;
                chevron.family = ControlVisualRole::menu_item;
                chevron.role = PartRole::disclosure;
                chevron.mark = PartMark::chevron_right;
                chevron.bounds = {bounds.width - 18.0, 10.0, 8.0, 8.0};
                chevron.states = states;
                paint_part(painter, *this, chevron);
            }
        }

        void on_pointer(PointerEvent& event) override {
            if (item_.kind == MenuItemKind::separator) return;
            if (event.action == PointerAction::move) {
                impl_.hover(depth_, index_);
                event.handled = true;
            } else if (event.action == PointerAction::down &&
                       event.button == PointerButton::primary) {
                pressed_ = true;
                impl_.hover(depth_, index_);
                event.handled = true;
            } else if (event.action == PointerAction::up &&
                       event.button == PointerButton::primary) {
                const bool invoke = pressed_ && absolute_bounds().contains(event.position);
                pressed_ = false;
                if (invoke) impl_.activate(depth_, index_);
                event.handled = true;
            } else if (event.action == PointerAction::leave) {
                pressed_ = false;
            }
        }

        void on_key(KeyEvent& event) override {
            if (!focused_ || event.action != KeyAction::down) return;
            if (event.physical_key == PhysicalKey::down) {
                impl_.move_focus(depth_, index_, 1);
            } else if (event.physical_key == PhysicalKey::up) {
                impl_.move_focus(depth_, index_, -1);
            } else if (event.physical_key == PhysicalKey::home) {
                impl_.focus_edge(depth_, false);
            } else if (event.physical_key == PhysicalKey::end) {
                impl_.focus_edge(depth_, true);
            } else if (event.physical_key == PhysicalKey::right) {
                if (item_.kind == MenuItemKind::submenu) {
                    impl_.open_submenu(depth_, index_, true);
                } else if (depth_ == 0U) {
                    static_cast<void>(impl_.navigate_root(1));
                }
            } else if (event.physical_key == PhysicalKey::left) {
                impl_.close_submenu(depth_);
            } else if (event.physical_key == PhysicalKey::enter ||
                       event.physical_key == PhysicalKey::space) {
                impl_.activate(depth_, index_);
            } else if (event.physical_key == PhysicalKey::escape) {
                impl_.close();
            } else {
                return;
            }
            event.handled = true;
        }

        void on_text_input(TextInputEvent& event) override {
            if (!focused_) return;
            const std::optional<char32_t> character = typed_character(event);
            if (character && impl_.type_mnemonic(depth_, index_, *character)) {
                event.handled = true;
            }
        }

        void on_focus_changed(bool focused) override {
            focused_ = focused;
            set_hot(focused);
            if (focused) impl_.ensure_visible(depth_, index_);
            invalidate(Dirty::paint | Dirty::semantics);
        }

        [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
            SemanticDescriptor descriptor;
            descriptor.role = item_.kind == MenuItemKind::separator
                ? SemanticRole::separator : SemanticRole::menu_item;
            descriptor.name = menu_display_text(item_.text);
            descriptor.access_key = mnemonic_access_key(item_.text);
            descriptor.description = item_.command_state.description;
            if (!item_.command_state.shortcut.empty()) {
                if (!descriptor.description.empty()) descriptor.description += " · ";
                descriptor.description += item_.command_state.shortcut;
            }
            if (!enabled_item() && !item_.command_state.availability_reason.empty()) {
                if (!descriptor.description.empty()) descriptor.description += " · ";
                descriptor.description += item_.command_state.availability_reason;
            } else if (!enabled_item() &&
                       item_.kind == MenuItemKind::submenu) {
                if (!descriptor.description.empty()) descriptor.description += " · ";
                descriptor.description += "No commands are currently available";
            }
            // A check or radio item can be checked whether or not it is now;
            // a command item while its command is checked.
            const bool can_check = item_.kind == MenuItemKind::check || item_.kind == MenuItemKind::radio ||
                                   item_.command_state.checked;
            if (can_check) descriptor.states |= SemanticState::checkable;
            if (item_.command_state.checked) descriptor.states |= SemanticState::checked;
            if (item_.kind == MenuItemKind::submenu &&
                impl_.submenu_open(depth_, index_)) {
                descriptor.states |= SemanticState::expanded;
            }
            descriptor.exposed = true;
            descriptor.actions = !enabled_item()
                ? std::vector<SemanticAction>{}
                : item_.kind == MenuItemKind::submenu
                ? std::vector<SemanticAction>{SemanticAction::focus,
                                              SemanticAction::expand,
                                              SemanticAction::collapse}
                : item_.kind == MenuItemKind::separator
                    ? std::vector<SemanticAction>{}
                    : std::vector<SemanticAction>{SemanticAction::focus,
                                                  SemanticAction::press};
            return descriptor;
        }

        bool on_semantic_action(SemanticAction action,
                                std::string_view) override {
            if (action == SemanticAction::focus) {
                if (window()) return (*window()).request_focus(shared_from_this());
                return false;
            }
            if (action == SemanticAction::press &&
                item_.kind != MenuItemKind::submenu && enabled_item()) {
                impl_.activate(depth_, index_);
                return true;
            }
            if (action == SemanticAction::expand &&
                item_.kind == MenuItemKind::submenu && enabled_item()) {
                impl_.open_submenu(depth_, index_, true);
                return true;
            }
            if (action == SemanticAction::collapse &&
                item_.kind == MenuItemKind::submenu) {
                impl_.remove_panels_after(depth_);
                return true;
            }
            return false;
        }

    protected:
        [[nodiscard]] bool mnemonic_matches(
            char32_t character) const noexcept override {
            return item_.kind != MenuItemKind::separator && enabled_item() &&
                   is_mnemonic(character, item_.text);
        }

        bool process_mnemonic_self(char32_t character) override {
            if (!mnemonic_matches(character)) return false;
            impl_.activate(depth_, index_);
            return true;
        }

    private:
        Impl& impl_;
        std::size_t depth_{};
        std::size_t index_{};
        const MenuSnapshot& item_;
        bool hot_{};
        bool focused_{};
        bool pressed_{};
    };

    void show(const Control::Ptr& invoker, Point position,
              const std::vector<MenuItemSpec>& specs, const MenuOpenMode mode) {
        close();
        if (!invoker || !(*invoker).attached_window()) {
            throw std::logic_error("ContextMenu requires an attached owner control");
        }
        snapshot = snapshot_items(specs);
        if (snapshot.empty()) {
            throw std::logic_error("ContextMenu has no visible items to show");
        }
        window = (*invoker).attached_window();
        owner_control = invoker;
        const Size client = (*window).client_size();
        layer = make_control<MenuLayer>(
            StableId(owner.stable_id_ + ".overlay.layer"), *this);
        (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
        // The layer opens first in every case: it takes outside clicks and,
        // with Popup windows, the owner's focus. The panels open in the
        // owner's warm Popup windows when the run has them (owner: menus
        // are Popups, before 0.5), else on the layer.
        overlay_token = (*window).open_overlay(invoker, layer);
        if (Event<>* closed = overlay_token.closed_event()) {
            overlay_revocation = (*closed).subscribe(
                owner, Delegate<>::bind<Impl, &Impl::on_overlay_revoked>(*this));
        }
        popup_mode = (*window).popup_windows_available();
        open_panel(0U, snapshot, position.x, position.y, {});
        if (panels.empty()) {
            close();
            return;
        }
        popup_mode = panels[0U].in_popup;
        Control::Ptr preferred = mode == MenuOpenMode::pointer ? panels[0U].panel : first_focusable(0U);
        if (!preferred) preferred = panels[0U].panel;
        if (popup_mode) {
            (*layer).set_focusable(true);
            focus_scope = (*window).begin_focus_scope(layer, layer);
            popup_closed = (*window).popup_window_closed().subscribe(
                owner, Delegate<std::size_t>::bind<Impl, &Impl::on_popup_closed>(*this));
            owner_active = (*window).active_changed().subscribe(
                owner, Delegate<bool>::bind<Impl, &Impl::on_owner_active>(*this));
            static_cast<void>(focus_row(0U, preferred));
        } else {
            focus_scope = (*window).begin_focus_scope(layer, preferred);
        }
        open = true;
        owner.open_changed_.emit(true);
    }

    void close() noexcept {
        if (closing) return;
        closing = true;
        if (window && focus_scope) {
            try {
                static_cast<void>((*window).end_focus_scope(focus_scope));
            } catch (...) {
            }
        }
        focus_scope = {};
        hide_popups();
        overlay_token.disconnect();
        overlay_revocation.disconnect();
        const bool changed = open;
        reset_visual_state();
        closing = false;
        if (changed && owner.is_alive()) owner.open_changed_.emit(false);
    }

    void on_overlay_revoked() noexcept {
        if (closing) return;
        if (window && focus_scope) {
            try {
                static_cast<void>((*window).end_focus_scope(
                    focus_scope, FocusScopeCloseReason::owner_unavailable));
            } catch (...) {
            }
        }
        focus_scope = {};
        overlay_revocation.disconnect();
        hide_popups();
        const bool changed = open;
        reset_visual_state();
        if (changed && owner.is_alive()) owner.open_changed_.emit(false);
    }

    // The owner's Popup window of a shown panel closed (the host destroyed
    // it), or the owner window stopped being active: the menu closes.
    void on_popup_closed(std::size_t slot) {
        if (slot < panels.size() && panels[slot].in_popup) close();
    }

    void on_owner_active(bool active) {
        if (!active) close();
    }

    // Hides every Popup window the menu shows, deepest first, and empties
    // their frames.
    void hide_popups() noexcept {
        for (std::size_t depth = panels.size(); depth > 0U; --depth) {
            PanelState& state = panels[depth - 1U];
            if (!state.in_popup) continue;
            if (window) (*window).hide_popup_window(depth - 1U);
            if (state.frame) (*state.frame).release();
            state.in_popup = false;
        }
        popup_closed.disconnect();
        owner_active.disconnect();
    }

    // The window that hosts the panel at `depth`: its Popup window's model
    // while it shows there, else the owner window.
    [[nodiscard]] Window* host_of(std::size_t depth) const noexcept {
        if (depth < panels.size() && panels[depth].in_popup && window) {
            Window* const model = (*window).popup_window_model(depth);
            if (model != nullptr) return model;
        }
        return window;
    }

    // Gives the menu's focus to `control` of the panel at `depth`, in the
    // window that hosts it; keys then go there.
    bool focus_row(std::size_t depth, const Control::Ptr& control) {
        Window* const host = host_of(depth);
        if (host == nullptr || !control) return false;
        focus_depth = depth;
        const bool focused = (*host).request_focus(control);
        return focused;
    }

    // A key or text event the layer received in the owner, passed to the
    // Popup whose panel has the menu's focus; false when the panels are not
    // in Popup windows.
    bool forward_key(const KeyEvent& event) {
        Window* const model = popup_mode ? host_of(focus_depth) : nullptr;
        if (model == nullptr || model == window) return false;
        const bool handled = (*model).dispatch_key(event);
        return handled;
    }

    bool forward_text(const TextInputEvent& event) {
        Window* const model = popup_mode ? host_of(focus_depth) : nullptr;
        if (model == nullptr || model == window) return false;
        const bool handled = (*model).dispatch_text(event);
        return handled;
    }

    // Shows the panel of `state` in the owner's Popup window `depth` at
    // `face` (owner client coordinates), framed with room for its shadow,
    // and starts its reveal. False when the window cannot show it.
    bool present_in_popup(std::size_t depth, PanelState& state, Rect face, bool upward) {
        if (!window || depth >= frames.size()) return false;
        std::shared_ptr<PopupFrame>& frame = frames[depth];
        if (!frame) {
            frame = make_control<PopupFrame>(
                StableId(owner.stable_id_ + ".popup.frame." + std::to_string(depth)));
        }
        (*state.panel).set_shadow((*window).popup_windows_translucent());
        const Insets outsets = (*state.panel).visual_outsets();
        (*frame).hold(state.panel, Rect{outsets.left, outsets.top, face.width, face.height});
        const Rect bounds{face.x - outsets.left, face.y - outsets.top,
                          face.width + outsets.left + outsets.right,
                          face.height + outsets.top + outsets.bottom};
        if (!(*window).show_popup_window(depth, frame, bounds)) {
            (*frame).release();
            return false;
        }
        state.frame = frame;
        state.in_popup = true;
        (*frame).reveal(upward);
        return true;
    }

    void reset_visual_state() noexcept {
        panels.clear();
        popup_mode = false;
        focus_depth = 0U;
        layer.reset();
        owner_control.reset();
        window = nullptr;
        snapshot.clear();
        open = false;
    }

    void open_panel(std::size_t depth, const std::vector<MenuSnapshot>& items,
                    double requested_x, double requested_y,
                    std::optional<std::size_t> parent_index) {
        if (!layer || !window || depth >= maximum_menu_depth) return;
        remove_panels_from(depth);
        const Size client = (*window).client_size();
        const double panel_width = std::max(
            0.0, std::min(owner.preferred_width.read() * text_scale(),
                          client.width - 8.0));
        double content = menu_border * 2.0;
        for (const MenuSnapshot& item : items) content += row_height(item, text_scale());
        const double maximum_height = std::max(command_row_height + menu_border * 2.0,
                                                client.height - 8.0);
        const double viewport = std::min(content, maximum_height);
        const double x = std::clamp(requested_x, 4.0,
                                    std::max(4.0, client.width - panel_width - 4.0));
        const double y = std::clamp(requested_y, 4.0,
                                    std::max(4.0, client.height - viewport - 4.0));
        PanelState state;
        state.items = &items;
        state.parent_index = parent_index;
        state.content_height = content;
        state.viewport_height = viewport;
        state.width = panel_width;
        state.panel = make_control<MenuPanel>(
            StableId(owner.stable_id_ + ".overlay.panel." + std::to_string(depth)),
            *this, depth);
        (*state.panel).set_requested_bounds({x, y, panel_width, viewport});
        state.face = Rect{x, y, panel_width, viewport};
        state.rows.reserve(items.size());
        for (std::size_t index = 0; index < items.size(); ++index) {
            std::shared_ptr<gui_forms::ContextMenu::Impl::MenuRow> row = make_control<MenuRow>(
                StableId(owner.stable_id_ + ".overlay.row." + items[index].stable_id),
                *this, depth, index, items[index]);
            (*state.panel).add_child(row);
            state.rows.push_back(std::move(row));
        }
        const Rect face = state.face;
        panels.push_back(std::move(state));
        PanelState& placed = panels.back();
        layout_rows(depth);
        // A panel clamped above where it was asked for opens upward.
        const bool upward = y < requested_y;
        if (!popup_mode || !present_in_popup(depth, placed, face, upward)) {
            (*placed.panel).set_shadow(true);
            (*layer).add_child(placed.panel);
        }
    }

    void layout_rows(std::size_t depth) {
        if (depth >= panels.size()) return;
        PanelState& state = panels[depth];
        double y = menu_border - state.scroll_offset;
        for (std::size_t index = 0; index < state.rows.size(); ++index) {
            const double height = row_height((*state.items)[index], text_scale());
            (*state.rows[index]).set_requested_bounds(
                {menu_border, y,
                 std::max(0.0, state.width - menu_border * 2.0), height});
            (*state.rows[index]).visible = y + height > menu_border && y < state.viewport_height - menu_border;
            y += height;
        }
    }

    void remove_panels_from(std::size_t depth) {
        while (panels.size() > depth) {
            const std::shared_ptr<MenuPanel> panel = panels.back().panel;
            if (panels.back().in_popup) {
                if (window) (*window).hide_popup_window(panels.size() - 1U);
                if (panels.back().frame) (*panels.back().frame).release();
            }
            panels.pop_back();
            if (layer && panel && (*panel).parent().get() == layer.get()) {
                static_cast<void>((*layer).remove_child((*panel).runtime_id()));
            }
        }
        if (depth > 0U && depth - 1U < panels.size()) {
            panels[depth - 1U].child_source_index.reset();
        }
    }

    void remove_panels_after(std::size_t depth) {
        remove_panels_from(depth + 1U);
    }

    [[nodiscard]] Control::Ptr first_focusable(std::size_t depth) const {
        if (depth >= panels.size()) return {};
        for (const std::shared_ptr<gui_forms::ContextMenu::Impl::MenuRow>& row : panels[depth].rows) {
            if ((*row).focusable() && (*row).effectively_visible()) return row;
        }
        for (const std::shared_ptr<gui_forms::ContextMenu::Impl::MenuRow>& row : panels[depth].rows) {
            if ((*row).focusable()) return row;
        }
        return {};
    }

    void ensure_visible(std::size_t depth, std::size_t index) {
        if (depth >= panels.size() || index >= panels[depth].rows.size()) return;
        PanelState& state = panels[depth];
        double top = menu_border;
        for (std::size_t current = 0; current < index; ++current) {
            top += row_height((*state.items)[current], text_scale());
        }
        const double bottom = top + row_height((*state.items)[index], text_scale());
        const double visible_top = state.scroll_offset + menu_border;
        const double visible_bottom = state.scroll_offset + state.viewport_height - menu_border;
        if (top < visible_top) state.scroll_offset = std::max(0.0, top - menu_border);
        else if (bottom > visible_bottom) {
            state.scroll_offset = std::min(
                std::max(0.0, state.content_height - state.viewport_height),
                bottom - state.viewport_height + menu_border);
        }
        layout_rows(depth);
    }

    void scroll(std::size_t depth, int rows) {
        if (depth >= panels.size()) return;
        PanelState& state = panels[depth];
        const double maximum = std::max(0.0,
            state.content_height - state.viewport_height);
        const double next = std::clamp(
            state.scroll_offset + static_cast<double>(rows) * command_row_height *
                text_scale(),
            0.0, maximum);
        if (next == state.scroll_offset) return;
        state.scroll_offset = next;
        layout_rows(depth);
    }

    void hover(std::size_t depth, std::size_t index) {
        if (depth >= panels.size() || index >= panels[depth].rows.size()) return;
        for (std::size_t current = 0; current < panels[depth].rows.size(); ++current) {
            (*panels[depth].rows[current]).set_hot(current == index);
        }
        if (window && (*panels[depth].rows[index]).focusable()) {
            static_cast<void>(focus_row(depth, panels[depth].rows[index]));
        }
        const MenuSnapshot& item = (*panels[depth].items)[index];
        if (item.kind == MenuItemKind::submenu) {
            open_submenu(depth, index, false);
        } else {
            remove_panels_after(depth);
        }
    }

    void open_submenu(std::size_t depth, std::size_t index, bool focus_first) {
        if (depth >= panels.size() || index >= panels[depth].rows.size()) return;
        const MenuSnapshot& item = (*panels[depth].items)[index];
        if (item.kind != MenuItemKind::submenu || item.children.empty() ||
            !snapshot_enabled(item)) return;
        if (panels[depth].child_source_index != index ||
            panels.size() <= depth + 1U) {
            // In the owner's client coordinates: the parent's face, and the
            // row's offset within its panel wherever the panel shows.
            const Rect parent = panels[depth].face;
            const Rect panel_bounds = (*panels[depth].panel).absolute_bounds();
            const Rect row_bounds = (*panels[depth].rows[index]).absolute_bounds();
            const Rect row{parent.x, parent.y + (row_bounds.y - panel_bounds.y), parent.width, row_bounds.height};
            const Size client = (*window).client_size();
            const double child_width = std::max(
                0.0, std::min(owner.preferred_width.read() * text_scale(),
                              client.width - 8.0));
            double x = parent.x + parent.width - 3.0;
            if (x + child_width + 4.0 > client.width) {
                x = parent.x - child_width + 3.0;
            }
            panels[depth].child_source_index = index;
            open_panel(depth + 1U, item.children, x, row.y, index);
            panels[depth].child_source_index = index;
        }
        if (focus_first && window) {
            if (const Control::Ptr first = first_focusable(depth + 1U)) {
                static_cast<void>(focus_row(depth + 1U, first));
            }
        }
    }

    void close_submenu(std::size_t depth) {
        if (depth == 0U) {
            if (navigate_root(-1)) return;
            close();
            return;
        }
        const std::optional<std::size_t> parent = panels[depth].parent_index;
        remove_panels_from(depth);
        if (window && parent && depth - 1U < panels.size() &&
            *parent < panels[depth - 1U].rows.size()) {
            static_cast<void>(focus_row(depth - 1U, panels[depth - 1U].rows[*parent]));
        }
    }

    void move_focus(std::size_t depth, std::size_t index, int direction) {
        if (depth >= panels.size() || panels[depth].rows.empty()) return;
        const std::size_t count = panels[depth].rows.size();
        for (std::size_t step = 1U; step <= count; ++step) {
            const std::ptrdiff_t raw = static_cast<std::ptrdiff_t>(index) +
                static_cast<std::ptrdiff_t>(direction) *
                    static_cast<std::ptrdiff_t>(step);
            const std::size_t candidate = static_cast<std::size_t>(
                (raw % static_cast<std::ptrdiff_t>(count) +
                 static_cast<std::ptrdiff_t>(count)) %
                static_cast<std::ptrdiff_t>(count));
            if ((*panels[depth].rows[candidate]).focusable()) {
                ensure_visible(depth, candidate);
                if (window) static_cast<void>(focus_row(depth, panels[depth].rows[candidate]));
                return;
            }
        }
    }

    void focus_edge(std::size_t depth, bool last) {
        if (depth >= panels.size()) return;
        std::vector<std::shared_ptr<MenuRow>>& rows = panels[depth].rows;
        if (last) {
            for (std::size_t index = rows.size(); index > 0U; --index) {
                if ((*rows[index - 1U]).focusable()) {
                    ensure_visible(depth, index - 1U);
                    if (window) static_cast<void>(focus_row(depth, rows[index - 1U]));
                    return;
                }
            }
        } else {
            for (std::size_t index = 0U; index < rows.size(); ++index) {
                if ((*rows[index]).focusable()) {
                    ensure_visible(depth, index);
                    if (window) static_cast<void>(focus_row(depth, rows[index]));
                    return;
                }
            }
        }
    }

    void activate(std::size_t depth, std::size_t index) {
        if (depth >= panels.size() || index >= panels[depth].rows.size()) return;
        const MenuSnapshot& item = (*panels[depth].items)[index];
        if (item.kind == MenuItemKind::submenu) {
            open_submenu(depth, index, true);
            return;
        }
        if (item.kind == MenuItemKind::separator || !item.command ||
            !item.command_state.enabled) return;
        const std::string source_id = std::string(
            (*panels[depth].rows[index]).stable_id().value());
        const std::string item_id = item.stable_id;
        const std::string command_id = (*item.command).stable_id();
        const std::shared_ptr<Command> command = item.command;
        // Release containment and restore the invoker before application code
        // runs. A command may deliberately focus its resulting surface; doing
        // this after execution would overwrite that destination with the menu
        // invoker during overlay teardown.
        if (window && focus_scope) {
            static_cast<void>((*window).end_focus_scope(focus_scope));
        }
        focus_scope = {};
        bool executed = false;
        if (window != nullptr) {
            executed = (*command).execute_routed(*window, source_id);
        } else {
            executed = (*command).execute(source_id);
        }
        if (executed && owner.is_alive()) {
            MenuItemInvocation invocation{owner.stable_id_, item_id,
                                          command_id, source_id};
            owner.item_invoked_.emit(invocation);
        }
        close();
    }

    // A typed character in the panel at depth. One enabled item whose
    // mnemonic matches is activated (a submenu opens); several take the
    // focus in turn, starting after the focused row. Returns false when no
    // item matches.
    bool type_mnemonic(std::size_t depth, std::optional<std::size_t> focused,
                       char32_t character) {
        if (depth >= panels.size() || !panels[depth].items) return false;
        const PanelState& state = panels[depth];
        const std::size_t count = std::min((*state.items).size(), state.rows.size());
        std::vector<std::size_t> matches;
        for (std::size_t index = 0U; index < count; ++index) {
            const MenuSnapshot& item = (*state.items)[index];
            if (item.kind != MenuItemKind::separator && snapshot_enabled(item) &&
                is_mnemonic(character, item.text)) {
                matches.push_back(index);
            }
        }
        if (matches.empty()) return false;
        if (matches.size() == 1U) {
            activate(depth, matches.front());
            return true;
        }
        std::size_t next = matches.front();
        if (focused) {
            for (const std::size_t index : matches) {
                if (index > *focused) {
                    next = index;
                    break;
                }
            }
        }
        ensure_visible(depth, next);
        if (window) static_cast<void>(focus_row(depth, panels[depth].rows[next]));
        return true;
    }

    [[nodiscard]] bool submenu_open(std::size_t depth,
                                    std::size_t index) const noexcept {
        return depth < panels.size() &&
               panels[depth].child_source_index == index &&
               panels.size() > depth + 1U;
    }

    [[nodiscard]] bool navigate_root(int direction) {
        return root_navigation_handler && root_navigation_handler(direction);
    }

    ContextMenu& owner;
    Window* window{};
    Control::Ptr owner_control;
    std::shared_ptr<MenuLayer> layer;
    std::vector<MenuSnapshot> snapshot;
    std::vector<PanelState> panels;
    OverlayToken overlay_token;
    SubscriptionToken overlay_revocation;
    FocusScopeId focus_scope{};
    // The panels show in the owner's Popup windows (decided per show).
    bool popup_mode{};
    // The depth of the panel holding the menu's focus.
    std::size_t focus_depth{};
    // The Popup frames, made on first use and kept (warm) per depth.
    std::array<std::shared_ptr<PopupFrame>, maximum_menu_depth> frames{};
    SubscriptionToken popup_closed;
    SubscriptionToken owner_active;
    bool open{};
    bool closing{};
    std::function<bool(int)> root_navigation_handler;
    std::function<bool(const PointerEvent&)> outside_pointer_handler;
};

ContextMenu::ContextMenu(std::string stable_id)
    : stable_id_(std::move(stable_id)), impl_(std::make_unique<Impl>(*this)) {
    if (!valid_stable_id(stable_id_)) {
        throw std::invalid_argument(
            "ContextMenu identity must be '.'-separated segments of lowercase ASCII letters and digits "
            "joined by single '-'");
    }
}

ContextMenu::~ContextMenu() {
    close();
}

void ContextMenu::set_items(std::vector<MenuItemSpec> items) {
    if (!is_alive()) throw std::logic_error("disposed ContextMenu cannot change items");
    std::unordered_set<std::string> identities;
    std::size_t count{};
    validate_specs(items, identities, 1U, count);
    close();
    items_ = std::move(items);
}

// A changed width closes an open menu first.
void ContextMenu::prepare_write(const decltype(preferred_width)&, double& incoming) {
    if (!is_alive()) {
        throw std::logic_error(
            "disposed ContextMenu cannot change preferred width");
    }
    if (!std::isfinite(incoming) || incoming < 120.0 || incoming > 1024.0) {
        throw std::invalid_argument(
            "ContextMenu preferred width must be finite and between 120 and 1024");
    }
    if (preferred_width.read() == incoming) return;
    close();
}

void ContextMenu::written(const decltype(preferred_width)&) {}

void ContextMenu::show(const Control::Ptr& owner, const Point window_position, const MenuOpenMode mode) {
    if (!is_alive()) throw std::logic_error("disposed ContextMenu cannot open");
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer)
        throw std::invalid_argument("ContextMenu opening mode is invalid");
    if (!std::isfinite(window_position.x) || !std::isfinite(window_position.y)) {
        throw std::invalid_argument("ContextMenu position must be finite");
    }
    (*impl_).show(owner, window_position, items_, mode);
}

void ContextMenu::close() noexcept {
    if (impl_) (*impl_).close();
}

bool ContextMenu::is_open() const noexcept {
    return impl_ && (*impl_).open;
}

void ContextMenu::set_root_navigation_handler(
    std::function<bool(int)> handler) {
    (*impl_).root_navigation_handler = std::move(handler);
}

void ContextMenu::set_outside_pointer_handler(
    std::function<bool(const PointerEvent&)> handler) {
    (*impl_).outside_pointer_handler = std::move(handler);
}

void ContextMenu::on_dispose() noexcept {
    close();
}

namespace {

// The events ContextMenu adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 2> context_menu_interactions{{
    {.name = "item_invoked", .payload = "MenuItemInvocation", .mutable_payload = false},
    {.name = "open_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void ContextMenu::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), context_menu_interactions.begin(), context_menu_interactions.end());
}

// property_codemod begin ContextMenu (tools/property_families/context_menu.json; generated, do not edit)

// property_codemod end ContextMenu

} // namespace gui_forms
