#pragma once
#include "gui_forms/surface_root/control_table/control_table.hpp"
#include "gui_forms/types/cursor_image/cursor_image.hpp"

#include "gui_forms/anatomy.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/binding_types.hpp"
#include "gui_forms/binding/property_table/property_table.hpp"
#include "gui_forms/control/control_capabilities/control_capabilities.hpp"
#include "gui_forms/control/control_layout_styles/control_layout_styles.hpp"
#include "gui_forms/display.hpp"
#include "gui_forms/dispatcher.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/events.hpp"
#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/semantics.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/types.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <atomic>
#include <any>
#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace gui_forms {

class Window;
class SurfaceRoot;
class Control;
class Painter;
struct PartPaint;
class ContainerControl;
class CommandTarget;
class ErrorProvider;
class HelpProvider;
class Binding;
class ControlBindingsCollection;
struct HelpRequestEvent;

namespace detail {
class DisplayChunk;
struct DispatcherState;
struct ControlInlineStyle;
struct InlineRestyle;

template <typename EventType, typename... Values>
class DeferredEventPublication final {
public:
    DeferredEventPublication(EventType& event, Values... values)
        : event_(&event), values_(std::move(values)...) {}

    void operator()() {
        emit(std::index_sequence_for<Values...>{});
    }

private:
    template <std::size_t... Indexes>
    void emit(std::index_sequence<Indexes...>) {
        (*event_).emit(std::get<Indexes>(values_)...);
    }

    EventType* event_{};
    std::tuple<Values...> values_;
};
}

struct RuntimeId {
    std::uint64_t value{};
    friend constexpr bool operator==(const RuntimeId& left,
                                     const RuntimeId& right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(
        const RuntimeId& left, const RuntimeId& right) noexcept {
        return left.value <=> right.value;
    }
};

// Exact WinForms flag values. Masked bounds mutation is kept in the portable
// retained core because layout authors and generated facades need one coherent
// source of truth for partial geometry updates.
enum class BoundsSpecified : std::uint8_t {
    none = 0U,
    x = 1U,
    y = 2U,
    width = 4U,
    height = 8U,
    location = 3U,
    size = 12U,
    all = 15U,
};

enum class GetChildAtPointSkip : std::uint8_t {
    none = 0U,
    invisible = 1U,
    disabled = 2U,
    transparent = 4U,
};

enum class OwnerDecorationLayer : std::uint8_t {
    before_content,
    after_content,
};

struct OwnerDecorationRecipe final {
    OwnerDecorationLayer layer{OwnerDecorationLayer::after_content};
    Size size{};
    std::optional<double> left;
    std::optional<double> top;
    std::optional<double> right;
    std::optional<double> bottom;
    double rotation_degrees{};
    MaterialBorderEdges border_edges;
    friend bool operator==(const OwnerDecorationRecipe& left_value,
                           const OwnerDecorationRecipe& right_value) noexcept {
        return left_value.layer == right_value.layer &&
               left_value.size == right_value.size &&
               left_value.left == right_value.left &&
               left_value.top == right_value.top &&
               left_value.right == right_value.right &&
               left_value.bottom == right_value.bottom &&
               left_value.rotation_degrees == right_value.rotation_degrees &&
               left_value.border_edges == right_value.border_edges;
    }
};

enum class AutoValidate : std::int8_t {
    inherit = -1,
    disable = 0,
    enable_prevent_focus_change = 1,
    enable_allow_focus_change = 2,
};

// Exact WinForms values. The portable core retains the result independently
// from any native modal loop; a host/facade decides whether a non-None result
// closes a currently modal top-level presentation.
enum class DialogResult : std::uint8_t {
    none = 0,
    ok = 1,
    cancel = 2,
    abort = 3,
    retry = 4,
    ignore = 5,
    yes = 6,
    no = 7,
    try_again = 10,
    continue_ = 11,
};

enum class ValidationConstraints : std::uint8_t {
    none = 0U,
    selectable = 1U << 0U,
    enabled = 1U << 1U,
    visible = 1U << 2U,
    tab_stop = 1U << 3U,
    immediate_children = 1U << 4U,
};

[[nodiscard]] constexpr ValidationConstraints operator|(
    ValidationConstraints left, ValidationConstraints right) noexcept {
    return static_cast<ValidationConstraints>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr ValidationConstraints operator&(
    ValidationConstraints left, ValidationConstraints right) noexcept {
    return static_cast<ValidationConstraints>(
        static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
}

constexpr ValidationConstraints& operator|=(ValidationConstraints& left,
                                             ValidationConstraints right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_validation_constraint(
    ValidationConstraints value, ValidationConstraints flag) noexcept {
    return (value & flag) != ValidationConstraints::none;
}

struct ControlValidationEvent final {
    Control* control{};
    Control* destination{};
    bool bulk{};
    bool cancel{};
};

// Renderer-neutral snapshot of a WinForms-style per-control layout
// transaction. Requested and committed revisions make deferred work
// observable without exposing a platform layout engine.
struct LayoutTransactionState final {
    std::uint32_t suspend_depth{};
    bool deferred{};
    std::uint64_t requested_revision{};
    std::uint64_t committed_revision{};
};

// WinForms-compatible mnemonic text is renderer-neutral retained state. A
// single '&' marks the following Unicode scalar; '&&' displays one literal
// ampersand. Matching currently applies Unicode identity plus ASCII case
// folding, which covers the desktop command vocabulary without importing a
// locale-dependent platform text API into the core.
struct MnemonicText final {
    std::string display_text;
    std::optional<char32_t> mnemonic;
};

[[nodiscard]] MnemonicText parse_mnemonic_text(std::string_view text);
// The character the first '&' marks, as UTF-8 (a semantic node's
// access_key, 12.13), or empty when `text` marks none.
[[nodiscard]] std::string mnemonic_access_key(std::string_view text);
[[nodiscard]] bool is_mnemonic(char32_t character,
                               std::string_view text) noexcept;

// Where a control's plain tooltip (Control::tool_tip_text) shows. Its
// window's one presenter reads it; a control type chooses it once, in its
// constructor.
// - bubble: a bubble near the pointer when it rests over the control, and
//   below the control when keyboard focus reaches it.
// - field_hint: a typed-text field (TextBox, NumericUpDown and the like).
//   Hover shows the bubble; keyboard focus shows the text inside the empty
//   field in light grey instead, as placeholder text, which the control
//   paints itself. A placeholder the program set wins.
// - owner_item: shown only over the item of an owning control that stands
//   for this one, such as a TabPage's tab header; never over the control's
//   own body.
enum class ToolTipPresentation : std::uint8_t { bubble, field_hint, owner_item };

// Forms-compatible style requests are retained as control state. GUI.Forms is
// always coherently composited, so clearing a buffering bit never opts a
// control into visible partial painting.
enum class ControlStyles : std::uint32_t {
    none = 0U,
    container_control = 1U << 0U,
    user_paint = 1U << 1U,
    opaque = 1U << 2U,
    resize_redraw = 1U << 3U,
    fixed_width = 1U << 4U,
    fixed_height = 1U << 5U,
    standard_click = 1U << 6U,
    selectable = 1U << 7U,
    user_mouse = 1U << 8U,
    supports_transparent_back_color = 1U << 9U,
    standard_double_click = 1U << 10U,
    all_painting_in_one_pass = 1U << 11U,
    cache_text = 1U << 12U,
    enable_notify_message = 1U << 13U,
    double_buffer = 1U << 14U,
    optimized_double_buffer = 1U << 15U,
    use_text_for_accessibility = 1U << 16U,
};

[[nodiscard]] constexpr ControlStyles operator|(ControlStyles left,
                                                ControlStyles right) noexcept {
    return static_cast<ControlStyles>(static_cast<std::uint32_t>(left) |
                                      static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr ControlStyles operator&(ControlStyles left,
                                                ControlStyles right) noexcept {
    return static_cast<ControlStyles>(static_cast<std::uint32_t>(left) &
                                      static_cast<std::uint32_t>(right));
}

constexpr ControlStyles& operator|=(ControlStyles& left,
                                    ControlStyles right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr BoundsSpecified operator|(BoundsSpecified left,
                                                   BoundsSpecified right) noexcept {
    return static_cast<BoundsSpecified>(static_cast<std::uint8_t>(left) |
                                        static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr BoundsSpecified operator&(BoundsSpecified left,
                                                   BoundsSpecified right) noexcept {
    return static_cast<BoundsSpecified>(static_cast<std::uint8_t>(left) &
                                        static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool has_bounds_specified(
    BoundsSpecified value, BoundsSpecified flag) noexcept {
    return (value & flag) != BoundsSpecified::none;
}

[[nodiscard]] constexpr GetChildAtPointSkip operator|(
    GetChildAtPointSkip left, GetChildAtPointSkip right) noexcept {
    return static_cast<GetChildAtPointSkip>(static_cast<std::uint8_t>(left) |
                                            static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr GetChildAtPointSkip operator&(
    GetChildAtPointSkip left, GetChildAtPointSkip right) noexcept {
    return static_cast<GetChildAtPointSkip>(static_cast<std::uint8_t>(left) &
                                            static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool has_child_skip(GetChildAtPointSkip value,
                                             GetChildAtPointSkip flag) noexcept {
    return (value & flag) != GetChildAtPointSkip::none;
}

class Control : public Component,
                public std::enable_shared_from_this<Control>,
                public ControlCapabilities<Control> {
public:
    using Ptr = std::shared_ptr<Control>;
    using WeakPtr = std::weak_ptr<Control>;
    // Compound controls opt in explicitly by hiding this constant with true.
    // make_control never infers lifecycle behavior from method presence.
    static constexpr bool initialize_tree_after_construction = false;

    explicit Control(StableId stable_id);
    ~Control() override;
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;

    [[nodiscard]] RuntimeId runtime_id() const noexcept { return runtime_id_; }
    [[nodiscard]] const StableId& stable_id() const noexcept { return stable_id_; }
    [[nodiscard]] Event<const std::string&>& name_changed() noexcept {
        return name_changed_;
    }
    [[nodiscard]] Event<bool>& visible_changed() noexcept {
        return visible_changed_;
    }
    [[nodiscard]] Event<bool>& enabled_changed() noexcept {
        return enabled_changed_;
    }
    // Application-owned metadata/lifetime anchor. Tag is deliberately inert:
    // assigning it does not imply rendering, layout, semantics, or binding.
    [[nodiscard]] const std::any& tag() const noexcept { return tag_; }
    void set_tag(std::any tag);
    [[nodiscard]] Ptr parent() const noexcept { return parent_.lock(); }
    [[nodiscard]] std::span<const Ptr> children() const noexcept { return children_; }
    [[nodiscard]] bool attached() const noexcept { return surface_root_ != nullptr; }

    void add_child(Ptr child);
    [[nodiscard]] Ptr remove_child(RuntimeId child);
    bool set_child_index(RuntimeId child, std::size_t index);
    [[nodiscard]] std::optional<std::size_t> child_index(
        RuntimeId child) const noexcept;
    void clear_children();

    [[nodiscard]] Rect requested_bounds() const noexcept { return requested_bounds_; }
    void set_requested_bounds(Rect bounds);
    void set_bounds(Rect values,
                    BoundsSpecified specified = BoundsSpecified::all);
    [[nodiscard]] double left() const noexcept { return requested_bounds_.x; }
    [[nodiscard]] double top() const noexcept { return requested_bounds_.y; }
    [[nodiscard]] double width() const noexcept { return requested_bounds_.width; }
    [[nodiscard]] double height() const noexcept { return requested_bounds_.height; }
    [[nodiscard]] double right() const noexcept {
        return requested_bounds_.x + requested_bounds_.width;
    }
    [[nodiscard]] double bottom() const noexcept {
        return requested_bounds_.y + requested_bounds_.height;
    }
    [[nodiscard]] Rect arranged_bounds() const;
    [[nodiscard]] Rect committed_arranged_bounds() const noexcept { return arranged_bounds_; }
    // The slot a parent's arrange just assigned, before the window commits
    // it; the committed bounds when none is pending. A scrolling parent
    // offsets this, never the committed bounds, which already carry its
    // previous offset.
    [[nodiscard]] Rect pending_layout_bounds() const noexcept {
        return layout_slot_ ? *layout_slot_ : arranged_bounds_;
    }
    [[nodiscard]] Rect client_rectangle() const noexcept {
        return {0.0, 0.0, arranged_bounds_.width, arranged_bounds_.height};
    }
    [[nodiscard]] virtual Rect display_rectangle() const noexcept {
        return client_rectangle();
    }
    [[nodiscard]] Rect absolute_bounds() const;
    [[nodiscard]] Point point_to_window(Point local) const;
    [[nodiscard]] Point point_from_window(Point window_point) const;
    [[nodiscard]] Rect rectangle_to_window(Rect local) const;
    [[nodiscard]] Rect rectangle_from_window(Rect window_rectangle) const;
    [[nodiscard]] bool contains(const Control& candidate) const noexcept;
    [[nodiscard]] Ptr get_child_at_point(
        Point client_point,
        GetChildAtPointSkip skip = GetChildAtPointSkip::none) const;
    [[nodiscard]] Ptr get_next_control(const Ptr& control, bool forward) const;
    void bring_to_front();
    void send_to_back();
    [[nodiscard]] Window* attached_window() const noexcept { return window_; }
    // The surface root whose table holds this control (null while detached).
    // Inside a sub-surface attached_window() is null and this is not.
    [[nodiscard]] SurfaceRoot* attached_surface_root() const noexcept { return surface_root_; }
    // Resolves the authored font against the attached window's presentation
    // text scale. Custom controls should use this for both measurement and
    // painting so text, wrapping, hit geometry, and carets remain coherent.
    [[nodiscard]] double effective_text_scale() const noexcept;
    [[nodiscard]] FontSpec effective_font(FontSpec authored) const noexcept;
    // The control's primary text font (T4 fonts, owner ruling of
    // 2026-11-04), scaled as effective_font: `own`, the font the control
    // holds, with its theme's font fields over it (Theme::resolve_font),
    // unless code set the font (set_font_from_code), since inline code wins.
    // Controls take it for the text a theme styles, for measurement and
    // painting alike; their fixed secondary fonts take effective_font. The
    // result is cached against the theme, the window's media facts and the
    // control's rules, so a call costs a comparison once resolved.
    [[nodiscard]] FontSpec text_font(FontSpec own) const noexcept;
    // The primary text font before the text scale, with where each field
    // came from.
    [[nodiscard]] ResolvedFont resolved_text_font(FontSpec own) const noexcept;
    // Whether the control's theme declares it clear (Theme::declares_clarity,
    // owner decision 2026-11-04): a clear control reports no opaque cover
    // (report_opaque_cover), so that a surface's backdrop is drawn beneath
    // it. Cached as text_font is, and dropped with it.
    [[nodiscard]] bool declares_clarity() const noexcept;
    // Resolves an already-effective FontSpec (normally from effective_font)
    // through the attached terminal provider without applying text scale a
    // second time.
    [[nodiscard]] ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view utf8, FontSpec effective) const;
    // The attached window's line-break opportunities
    // (Window::fill_line_break_opportunities_utf8), or the estimate while
    // detached.
    [[nodiscard]] TextResolutionStatus fill_line_break_opportunities_utf8(
        std::string_view utf8, std::span<LineBreakOpportunity> opportunities) const;
    [[nodiscard]] double snap_text_baseline(double logical_baseline) const noexcept;
    // The theme the control is drawn in: the nearest override on the
    // control or an ancestor, else its window's application theme, else the
    // default theme; but GUI.Forms' own theme where that theme has no
    // variant for the window's colour scheme (T4 theme variants).
    [[nodiscard]] const Theme& effective_theme() const noexcept;
    // Whether the control is drawn in GUI.Forms' own theme in place of the
    // theme it was given (effective_theme): its back_color, fore_color,
    // authored surface material and parts' authored colours are then not
    // applied, since they were chosen against the other theme. They apply
    // again when that theme is shown.
    [[nodiscard]] bool application_colours_suspended() const noexcept;
    // Whether back_color or fore_color has been set on this control, which
    // writes the warning W404 `inline_colour` once for each (T4 theme
    // variants, stage A: colour belongs in the theme).
    [[nodiscard]] bool back_color_warned() const noexcept { return back_color_warned_; }
    [[nodiscard]] bool fore_color_warned() const noexcept { return fore_color_warned_; }
    // The attached window's key map (Window::key_map), or the PC
    // convention's while detached. A control asks it whether a key event is
    // a named keyboard command instead of naming the keys itself.
    [[nodiscard]] const KeyMap& effective_key_map() const noexcept;
    [[nodiscard]] std::shared_ptr<const Theme> theme_override() const noexcept {
        return theme_override_;
    }
    // Setting or clearing the override changes the theme in effect for the
    // subtree: each inline style there defining custom properties is
    // compiled against the new theme first, and where one does not compile
    // the change is refused, invalid_argument, with nothing changed (P12b).
    // A user theme (ThemeRuleSet::user) is invalid_argument too.
    void set_theme_override(std::shared_ptr<const Theme> theme);
    void clear_theme_override();
    [[nodiscard]] const std::optional<SurfaceMaterial>&
    authored_surface_material() const noexcept {
        return authored_surface_material_;
    }
    void set_authored_surface_material(SurfaceMaterial material);
    void clear_authored_surface_material();
    // The background and foreground colours the application authored for
    // this control (WinForms' BackColor and ForeColor), or none: the theme's
    // colours stand. They are the T4-1 authored colours of the control's
    // parts (PartPaint::authored_background and authored_foreground), with
    // the same rules: a part's own authored colour, the most specific,
    // stands over them; the theme's important declarations stand over them;
    // forced high contrast drops them; and a disabled part keeps the
    // theme's disabled colours, so disabled text stays greyed.
    // - The background paints the control's bounds before its content, as
    //   WinForms' OnPaintBackground does, and fills the control's grounds
    //   (faces, wells, plates) flat, keeping their borders.
    // - The foreground draws the control's text, marks and rules.
    // They reach what a control paints through paint_part; a control that
    // draws its own text or surfaces directly reads ambient_back_color and
    // ambient_fore_color.
    // Ambient inheritance, as WinForms: a control without its own colour
    // takes its nearest ancestor's (ambient_back_color, ambient_fore_color),
    // so a colour set on a container flows to every descendant that sets
    // none. An inherited background is not painted again over the bounds
    // (the ancestor already painted it) and does not fill a well: an
    // editor's or a list's well keeps the theme's window colour, as WinForms'
    // text and list boxes keep SystemColors.Window under a coloured form.
    // A check box's box is a well too: it keeps the theme's colour under an
    // inherited background, but the check box's own background fills it,
    // where WinForms' box stays white. Writing one (std::nullopt clears it)
    // repaints the control and its descendants.
    // This control's own colour, else its nearest ancestor's, else none.
    [[nodiscard]] std::optional<Color> ambient_back_color() const noexcept;
    [[nodiscard]] std::optional<Color> ambient_fore_color() const noexcept;
    [[nodiscard]] std::span<const OwnerDecorationRecipe>
    owned_decorations() const noexcept {
        return owned_decorations_;
    }
    void set_owned_decorations(const OwnerDecorationRecipe* decorations,
                               std::size_t decoration_count);
    void clear_owned_decorations();
    [[nodiscard]] ControlVisualStatus visual_status() const noexcept {
        return visual_status_;
    }
    void set_visual_status(ControlVisualStatus status);
    [[nodiscard]] ControlVisualContext visual_context(
        bool hovered = false, bool pressed = false, bool selected = false,
        bool focused = false, bool defaulted = false) const noexcept;
    [[nodiscard]] bool invoke_required() const noexcept;
    [[nodiscard]] DispatchOperation begin_invoke(std::function<void()> callback);
    // Executes inline on the owning UI thread. A worker caller blocks without
    // pumping until the installed host drains the callback, then receives the
    // original exception or DispatchCancelledError.
    void invoke(std::function<void()> callback);
    [[nodiscard]] Point auto_scroll_offset() const noexcept {
        return auto_scroll_offset_;
    }
    void set_auto_scroll_offset(Point offset);
    [[nodiscard]] Event<bool>& auto_size_changed() noexcept {
        return auto_size_changed_;
    }
    [[nodiscard]] virtual Size get_preferred_size(Size proposed);

    // Per-control layout transactions are independent from Window update
    // scopes. Nested suspension preserves committed geometry; the final
    // ResumeLayout(true) performs at most one bounded retained flush.
    void suspend_layout();
    void resume_layout(bool perform_layout = true);
    void perform_layout();
    // Settles the surface root's pending touches first (see dirty()).
    [[nodiscard]] LayoutTransactionState layout_transaction_state() const;

    // Layout collapse is assigned only by a retained parent layout. It does
    // not mutate authored visibility, but it removes the subtree from the
    // effective visual/input/semantic surface until the parent reveals it.
    [[nodiscard]] bool layout_collapsed() const noexcept {
        return layout_collapsed_;
    }
    [[nodiscard]] bool focusable() const noexcept { return focusable_; }
    void set_focusable(bool focusable);
    [[nodiscard]] Event<bool>& causes_validation_changed() noexcept {
        return causes_validation_changed_;
    }
    [[nodiscard]] Event<ControlValidationEvent&>& validating() noexcept {
        return validating_;
    }
    [[nodiscard]] Event<>& validated() noexcept { return validated_; }
    // hit_test_transparent: a transparent hit target remains paintable and
    // queryable, but ordinary
    // pointer routing passes through it. GetChildAtPoint can include it or skip
    // it explicitly, matching the purpose of GetChildAtPointSkip::Transparent
    // without importing a platform window-style bit into the core.
    [[nodiscard]] ControlStyles styles() const noexcept { return styles_; }
    [[nodiscard]] bool has_style(ControlStyles style) const noexcept {
        return (styles_ & style) == style;
    }
    void set_style(ControlStyles style, bool enabled);
    [[nodiscard]] bool double_buffered() const noexcept {
        return has_style(ControlStyles::double_buffer) ||
               has_style(ControlStyles::optimized_double_buffer);
    }
    void set_double_buffered(bool enabled);
    // Style classes (T2 plan, E4): names a theme's class rules select, in
    // the order they were added, without duplicates. A class name is 1 to 64
    // characters of lowercase ASCII letters, digits, '-' and '_', starting
    // with a letter (valid_style_class_name). Changing the classes repaints
    // the control. set_classes throws std::invalid_argument for an invalid
    // name and then changes nothing; it drops repeated names.
    [[nodiscard]] std::span<const std::string> classes() const noexcept { return classes_; }
    void set_classes(std::vector<std::string> classes);
    // Adds or removes one class; false when the class was already there (or
    // absent). An invalid name throws std::invalid_argument.
    bool add_class(std::string_view name);
    bool remove_class(std::string_view name);
    [[nodiscard]] bool has_class(std::string_view name) const noexcept;
    // The control's inline style (T3, R2-10): declarations for the control,
    // then blocks for its own parts and states with its selector implied,
    // as `opacity: 0.9; border-radius: 6px !important; ::part(face):hover
    // { scale: 1.04 }`. The control owns it: it styles this control alone,
    // whatever its stable ID, and it survives theme replacement. In the
    // cascade (the owner's rule E1) an `!important` declaration wins
    // whichever side it is on, and where neither or both are important the
    // inline style wins; under forced high contrast only the inline
    // `!important` declarations that are not colours stand. A refused style
    // changes nothing: the control keeps its previous one, and the result
    // holds the diagnostics. Empty text clears the style. Its diagnostics
    // name the file `inline:<stable ID>`. A style defining custom
    // properties (`--accent: #c00; ::part(face) { fill: var(--accent) }`),
    // or reading one the theme defines, is compiled against the theme in
    // effect for the control (the default theme while it has none): where
    // its definitions win for a part, the theme's rules reading them read
    // the style's values, and where they do not, its own var()s read the
    // theme's. A change of that
    // theme compiles it again (ApplicationResources::set_theme,
    // set_theme_override, attachment).
    [[nodiscard]] InlineStyleResult set_inline_style(std::string_view text);
    // The inline style's text in force; empty without one.
    [[nodiscard]] std::string_view inline_style() const noexcept;
    // Its compiled rules (CompiledThemeRules::compile_inline), or null
    // without one; the cascade reads them for this control alone.
    [[nodiscard]] const CompiledThemeRules* inline_style_rules() const noexcept;
    // Whether the application marks this control's surface ceremonial (T2
    // GIVEN B3-1; design language §10): an installer's or a first run's
    // world. On it, and on its descendants, the theme engine's ambient
    // effects may cover more than 10 % of the window; everywhere else the
    // everyday cap holds. A theme never sets it. Changing it repaints the
    // control and its descendants.
    [[nodiscard]] bool ceremonial() const noexcept { return ceremonial_; }
    void set_ceremonial(bool ceremonial);
    // Whether this control or one of its ancestors is marked ceremonial.
    [[nodiscard]] bool on_ceremonial_surface() const noexcept;
    // A stock assignment clears the image override. Null images select fallback.
    void set_custom_cursor(CursorImagesPtr images, CursorKind fallback = CursorKind::arrow);
    [[nodiscard]] CursorImagesPtr effective_cursor_images() const noexcept;
    // tool_tip_text, the control's plain tooltip (owner, 2026-10-28): text,
    // never markup,
    // that its window's tooltip presenter shows as tool_tip_presentation()
    // says, when the window's and the program's switches allow. Empty, the
    // default, never shows. The text also reaches the control's semantic
    // node (SemanticNode::tool_tip). Throws std::invalid_argument for text
    // that is not valid UTF-8, changing nothing. Raises tool_tip_text_changed
    // when the text changes. Bindable as "tool_tip_text".
    [[nodiscard]] Event<const std::string&>& tool_tip_text_changed() noexcept {
        return tool_tip_text_changed_;
    }
    [[nodiscard]] ToolTipPresentation tool_tip_presentation() const noexcept {
        return tool_tip_presentation_;
    }
    // Whether this control's semantic virtual children may carry tooltips
    // of authored parts (SemanticNode::tool_tip), as a strip's items and a
    // tab control's headers do. The tooltip presenter looks for a part's
    // tooltip only in such a control; a list of data items never sets it,
    // with one exception (owner decision, 2026-11-02): DataGridView sets it
    // for its error glyphs, which are the grid's interface, not data, and
    // tooltip their error text. Its cells' contents still carry none.
    [[nodiscard]] bool publishes_item_tool_tips() const noexcept {
        return publishes_item_tool_tips_;
    }
    // The command target this control offers its window's command routing
    // chain while focus is on it or inside it (the focused route, owner
    // decision DK-29; see gui_forms/commands/command_route). The control
    // shares ownership, so a docking workspace may also name the same target
    // as an active panel's or document's. None by default.
    [[nodiscard]] const std::shared_ptr<CommandTarget>& command_target() const noexcept {
        return command_target_;
    }
    void set_command_target(std::shared_ptr<CommandTarget> target) noexcept {
        command_target_ = std::move(target);
    }
    [[nodiscard]] CursorKind effective_cursor() const noexcept;
    [[nodiscard]] bool effectively_visible() const noexcept;
    [[nodiscard]] bool effectively_enabled() const noexcept;
    [[nodiscard]] bool eligible_for_input() const noexcept;
    void set_pointer_capture(bool captured);
    [[nodiscard]] bool has_pointer_capture() const noexcept;

    // An attached control's flags are read after its surface root settles
    // the touches made since the last read (SurfaceRoot::settle_dirty): an
    // ancestor's flags are set by its descendants' touches at the settle.
    [[nodiscard]] Dirty dirty() const;
    [[nodiscard]] Dirty subtree_dirty() const;
    // This control's slot in its surface root's control table, for
    // inspection (SurfaceRoot::control_table); unassigned while detached.
    [[nodiscard]] detail::ControlSlot surface_slot() const noexcept { return surface_slot_; }
    [[nodiscard]] PaintPlane paint_plane() const noexcept { return paint_plane_; }
    void set_paint_plane(PaintPlane plane);
    [[nodiscard]] std::optional<DisplayChunkInfo> display_chunk_info() const noexcept;
    void invalidate(Dirty dirty);
    // Marks only a local client rectangle for repaint. The retained display
    // chunk remains the authoritative complete presentation, while host
    // damage and replay stay clipped to this bounded region.
    void invalidate(Rect local_damage);
    void invalidate_subtree(Dirty dirty);
    void invalidate_declared(Dirty declared_effects);

    void begin_init();
    void end_init();
    [[nodiscard]] bool initializing() const noexcept { return initialization_depth_ != 0; }
    [[nodiscard]] std::uint64_t initialization_depth() const noexcept {
        return initialization_depth_;
    }
    [[nodiscard]] Event<Dirty, bool>& initialization_completed() noexcept {
        return initialization_completed_;
    }
    [[nodiscard]] Event<const PointerEvent&>& pointer_observed() noexcept {
        return pointer_observed_;
    }
    // Reports this control's participation in the preview route before the
    // eventual target callback. Compound/application roots use it for bounded
    // click-away state transitions without replacing Window hit testing.
    [[nodiscard]] Event<const PointerEvent&>& pointer_preview_observed() noexcept {
        return pointer_preview_observed_;
    }
    [[nodiscard]] Event<bool>& focus_observed() noexcept {
        return focus_observed_;
    }
    [[nodiscard]] Event<Rect>& arranged_bounds_changed() noexcept {
        return arranged_bounds_changed_;
    }
    [[nodiscard]] Event<HelpRequestEvent&>& help_requested() noexcept {
        return help_requested_;
    }
    [[nodiscard]] ControlBindingsCollection& data_bindings();
    [[nodiscard]] const ControlBindingsCollection& data_bindings() const;
    // Control's own table (its capabilities' entries); a derived type joins
    // it first (join_property_tables) and returns the joined table.
    static constexpr std::size_t control_property_count = 22U;
    [[nodiscard]] static const std::array<PropertyEntry, control_property_count>& control_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;
    // The properties below are the instance registry's
    // (define_bindable_property) and the type's table's (Component::
    // property_table) together; a registration replaces a table entry of the
    // same name.
    [[nodiscard]] bool has_bindable_property(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> bindable_property_names() const;
    [[nodiscard]] std::optional<PropertyDescriptor> property_descriptor(
        std::string_view name) const;
    [[nodiscard]] std::vector<PropertyDescriptor> property_descriptors() const;
    [[nodiscard]] std::optional<BindingValue> property_value(
        std::string_view name) const;
    void set_property_value(std::string_view name, BindingValue value);
    [[nodiscard]] SubscriptionToken subscribe_property_changed(
        std::string_view name, Component& owner, std::function<void()> changed);
    // Returns false only when the named property does not exist or has no
    // declared reset path. A supported reset may be a no-op when already at
    // its default, matching deterministic ShouldSerialize behavior.
    bool reset_property(std::string_view name);
    [[nodiscard]] bool should_serialize_property(std::string_view name) const;
    [[nodiscard]] PropertyValueOrigin property_value_origin(
        std::string_view name) const;

    [[nodiscard]] virtual Size measure(Size available);
    virtual void arrange(Rect final_bounds);
    // The measure entry layout solvers call (stage L). It answers from this
    // control's measure cache when an entry was stored under exactly this
    // constraint since the control last became measure-dirty, and otherwise
    // calls measure_intrinsic once and stores the answer. The cache holds
    // four entries: one min-content width, one max-content width, and the
    // two most recent other queries. A result with a negative or non-finite
    // dimension is clamped to zero and counted. Throws std::invalid_argument,
    // with the cache unchanged, when valid_layout_constraint rejects the
    // constraint. An attached control is measured on its window's UI
    // thread only. The window's own measure pass calls this too, so a
    // container's later query under the same constraint is a hit.
    [[nodiscard]] Size measure_for_layout(const LayoutConstraint& constraint);
    // The baselines a layout container aligns this control by (stage L,
    // PLAN.md section 12.3): its first and last line boxes' baselines, as
    // offsets from the top of its border box when laid out at `width`. None
    // means it has no line box; the container then synthesizes one from the
    // border box, as CSS does. The defaults answer none. Text that lays out
    // in lines (TextBlock) and the CSS containers override them. Flex and
    // grid alignment use the first baseline; an inline item in BlockPanel
    // uses the last, as an inline-block does.
    [[nodiscard]] virtual std::optional<double> first_baseline(double width);
    [[nodiscard]] virtual std::optional<double> last_baseline(double width);
    // Advances whenever this control becomes measure-dirty; a cache entry
    // stored under an earlier generation is never answered. Settles first
    // (see dirty()): a descendant's touch advances it at the settle.
    [[nodiscard]] std::uint64_t measure_generation() const;
    // Layout boundaries (stage L, PLAN section 3.3). A descendant's measure
    // dirtiness marks an ancestor for measure only while the ancestor's size
    // can depend on it: while the ancestor's own measure reads its children
    // (size_depends_on_content) or its parent sizes it from its measure
    // (sizes_children_from_content). The first ancestor for which neither
    // holds is a layout boundary: it is marked for arrange only, and nothing
    // above it is marked. Both default to true, which keeps every existing
    // control's measure propagation unchanged; a container overrides them
    // only when it can state the answer for every configuration.
    [[nodiscard]] virtual bool size_depends_on_content() const noexcept;
    [[nodiscard]] virtual bool sizes_children_from_content() const noexcept;
    // The container's invalidation policy (property-capabilities study,
    // decision 9): the kinds of a descendant's touch that escalate to this
    // control's subtree mark instead of staying with the descendant (for
    // example measure, where one child's size change re-lays out every
    // child anyway). The nearest ancestor escalating a kind takes it, at
    // the settle. Constant per type. The default escalates nothing.
    [[nodiscard]] virtual Dirty child_touch_escalation() const noexcept;
    // Whether this container's arrange asks each child the content
    // questions its algorithm needs (min-content, max-content, a height at
    // a definite width) rather than the at-most question the window's
    // measure pass would ask. When true, the window's pass leaves a
    // measure-dirty direct child to its arrange: the child's dirtiness has
    // already emptied its cache, and measuring it against the parent's
    // available size too would cost a second callback whose answer nothing
    // reads (stage L, PLAN section 5: one item's text change costs one
    // callback). False by default, which keeps every existing container's
    // measure pass unchanged.
    [[nodiscard]] virtual bool measures_children_in_arrange() const noexcept;
    // layout_direction is the authored inline direction, `inherit` by
    // default (stage L, PLAN section 4); writing it invalidates this
    // subtree's arrangement.
    // The direction this control lays out in: its own when not `inherit`,
    // otherwise the nearest ancestor's, and left to right at a root.
    [[nodiscard]] LayoutDirection effective_layout_direction() const noexcept;
    virtual void on_paint(Painter& painter, Rect local_damage);
    // Bounded decoration may paint outside arranged bounds without changing
    // layout or hit testing. Parents still clip descendants to their client
    // rectangle. The compositor uses these outsets for damage and replay.
    [[nodiscard]] virtual Insets visual_outsets() const noexcept;
    // One ambient frame of the window's motion clock, for a control that
    // enlisted itself (MotionClock::enlist_control_ambient): advance what
    // moves to `now` and invalidate what it covers. Return false when the
    // control was not painted since its last frame (it is no longer shown):
    // the clock then drops it, so a control can never keep a window
    // painting by itself. The default returns false.
    virtual bool on_ambient_frame(FrameTime now);
    // Whether what on_paint and on_paint_overlay draw at a point changes
    // when only the control's size does. A control that answers false keeps
    // its pixels inside its old extent when it is resized in place, and only
    // the strips the resize exposed or vacated are repainted. The default is
    // the resize_redraw style, which is on unless the control clears it.
    [[nodiscard]] virtual bool paint_depends_on_size() const noexcept;
    // The axes of the control's own size along which its children's layout
    // changes: a resize along another axis leaves every child where it was.
    // Read by the arrange pass when the control's size changes and kept as
    // its resize registration in the control table, for resize damage and
    // the resize_relayouts counter; arrange runs either way (reduced M0.4).
    // When the last arrange ran Control::arrange (the dock and anchor
    // layout), the default reads that layout over the visible children:
    // docked fill answers both axes, top or bottom horizontal, left or
    // right vertical; undocked, a child held by its right edge (or by
    // neither side) answers horizontal and one held by its bottom edge (or
    // by neither) vertical, with left and right exchanged in a
    // right-to-left layout. A control whose arrange places its children by
    // another engine answers both axes when it has children, unless it
    // overrides this with its engine's answer.
    [[nodiscard]] virtual ResizeAxes layout_resize_response() const noexcept;
    [[nodiscard]] virtual bool hit_test_local(Point local_point) const;

    virtual void on_pointer_preview(PointerEvent& event);
    virtual void on_pointer(PointerEvent& event);
    virtual void on_pointer_bubble(PointerEvent& event);
    virtual void on_key_preview(KeyEvent& event);
    virtual void on_key(KeyEvent& event);
    virtual void on_key_bubble(KeyEvent& event);
    virtual void on_text_input(TextInputEvent& event);
    // Dialog characters are routed by Window only after the focused key route
    // and accelerators decline them. The base implementation traverses the
    // retained subtree in stable child order.
    virtual bool process_mnemonic(char32_t character);
    [[nodiscard]] virtual SemanticDescriptor semantic_descriptor() const;
    // Extender providers enrich the final semantic projection after a stock or
    // custom control has authored its own descriptor. This keeps validation
    // and help metadata from depending on every subclass remembering to call a
    // particular base implementation.
    void apply_provider_semantics(SemanticDescriptor& descriptor) const;
    [[nodiscard]] virtual std::vector<SemanticNode> semantic_virtual_children() const;
    virtual bool on_semantic_action(SemanticAction action,
                                    std::string_view value);
    virtual bool on_semantic_child_action(std::string_view stable_id,
                                          SemanticAction action,
                                          std::string_view value);
    // Editable text for hosts (owner decision A-T1, 12.13), reached only
    // through Window::semantic_text_layout and
    // Window::set_semantic_text_selection, which have already refused stale
    // identities and calls during paint. A control whose own node publishes
    // editable text fills `layout` (empty on entry) with offsets into the
    // value its descriptor publishes and returns done. It may build the
    // stale layout its next paint would build (such as a multiline box's
    // rows and scroll bars) but changes no text, selection or scroll
    // position. The base returns no_text.
    [[nodiscard]] virtual SemanticTextResult on_semantic_text_layout(SemanticTextLayout& layout);
    // Moves the selection of the published text to `selection` without
    // moving focus, as the user's own selection would (selection_changed is
    // raised; the text is unchanged). Refuses an offset off a grapheme
    // boundary with off_boundary and changes nothing then. The base returns
    // no_text.
    [[nodiscard]] virtual SemanticTextResult on_set_semantic_text_selection(SemanticTextSelection selection);
    virtual void on_drag_preview(DragEvent& event);
    virtual void on_drag(DragEvent& event);
    virtual void on_drag_bubble(DragEvent& event);
    virtual void on_focus_changed(bool focused);
    // Where an input method places its composition and candidate windows
    // while this control has focus: the caret's box, in local coordinates.
    // The base returns none, and SurfaceRoot::text_input_caret then uses a
    // one-unit-wide box at the control's left edge.
    [[nodiscard]] virtual std::optional<Rect> text_input_caret() const;
    // Window focus visibility is input-modality state, distinct from actual
    // focus. Controls with bounded focus-cue presentation may retarget here.
    virtual void on_focus_cue_changed(bool visible);
    virtual void on_activate();

    // Anatomy (stage T1). A control type opts in by overriding anatomy(),
    // publish_part_geometry() and anatomy_states() together; the defaults
    // publish base_control_anatomy(). The anatomy describes the type, never
    // the instance: it is the same record for every object of the type.
    [[nodiscard]] virtual const ControlAnatomy& anatomy() const noexcept;
    // Fills one record per declared part, in declaration order, in local
    // coordinates after layout. `geometry` must hold exactly
    // anatomy().parts.size() records; otherwise this throws
    // std::invalid_argument and leaves `geometry` unchanged. This is the
    // generic path for the engine, inspection and tools; C++ callers read
    // parts() instead.
    void fill_part_geometry(std::span<PartGeometry> geometry) const;
    // The active states. Every standard state in the result is one
    // anatomy().states declares, and every custom index is in range.
    [[nodiscard]] virtual AnatomyStateSet anatomy_states() const;

    // The parts and states every control publishes, as dot members. A type
    // that declares its own anatomy hides these with records of its own.
    // Every record reads the control's published geometry and states, so a
    // record read through a base type agrees with the dynamic type.
    struct Parts final {
        PartGeometry region;
        PartGeometry focus;
    };
    struct States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

    // Repeated items (stage T1, owner decision 5). A list, tree or grid
    // publishes its realized (visible) items, each with the parts and states
    // of its item system in anatomy() and the stable ID of its semantic
    // virtual child. `item` counts realized items in paint order across
    // every item system, not model positions. This is the generic path;
    // item-bearing types also offer typed records through realized_items().
    [[nodiscard]] virtual std::size_t realized_item_count() const;
    [[nodiscard]] virtual std::string realized_item_stable_id(std::size_t item) const;
    // The item system a realized item belongs to; the first by default.
    // Throws std::out_of_range for an item past realized_item_count().
    [[nodiscard]] ItemSystemIndex realized_item_system(std::size_t item) const;
    // Fills one record per part of the item's system for one realized item.
    // Throws std::out_of_range for an item past realized_item_count() and
    // std::invalid_argument when `geometry` does not hold exactly one record
    // per part of realized_item_system(item); `geometry` is then unchanged.
    void fill_realized_item_geometry(std::size_t item,
                                     std::span<PartGeometry> geometry) const;
    [[nodiscard]] virtual AnatomyStateSet realized_item_anatomy_states(
        std::size_t item) const;

protected:
    // Raised after a changed write of the property, its touch and side
    // effects done (P-8, option 1b; Windows Forms' OnVisibleChanged and
    // the others). The base raises the property's change event where the
    // control has one (visible, enabled, auto_size, causes_validation) and
    // does nothing for the rest until their events exist. An override
    // calls the base.
    virtual void on_visible_changed();
    virtual void on_enabled_changed();
    virtual void on_auto_size_changed();
    virtual void on_causes_validation_changed();
    virtual void on_cursor_changed();
    virtual void on_back_color_changed();
    virtual void on_fore_color_changed();
    virtual void on_margin_changed();
    virtual void on_padding_changed();
    virtual void on_dock_changed();
    virtual void on_layout_direction_changed();
    virtual void on_tab_index_changed();
    virtual void on_tab_stop_changed();

    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    // Whether code set the control's primary font (T4 fonts): a control's
    // set_font marks it, after validating the font, and a clear_font that
    // returns to the control's default unmarks it. Marked, text_font is
    // `own` scaled, whatever the theme declares.
    void set_font_from_code(bool from_code) noexcept;
    // The opaque cover hint (owner, 2026-10-28). Called from a paint
    // callback right after drawing an opaque solid fill of `local`, in this
    // control's coordinates, with nothing translated, clipped or grouped:
    // the control's display chunk then leaves `local` opaque whatever lay
    // beneath, and the window skips painting what lies under it (the
    // backplane and the controls painted before this one). Ignored unless
    // the window is recording this control into `painter`, and dropped if
    // the recording later begins a copy group.
    void report_opaque_cover(const Painter& painter, Rect local) const noexcept;
    // Answers one measure_for_layout miss; `constraint` is already valid.
    // The default maps the query onto measure(Size) through
    // measure_available_size: a definite or at-most axis passes its extent
    // and a content axis the practical unbounded extent, so a control that
    // cannot rewrap reports its preferred size as both its min-content and
    // its max-content size. A definite axis then reports exactly its extent.
    // Text that wraps (Label) overrides it to answer min-content from its
    // widest unbreakable segment.
    [[nodiscard]] virtual Size measure_intrinsic(const LayoutConstraint& constraint);
    // Called by fill_part_geometry with `geometry` already sized to
    // anatomy().parts.size() and every record reset to absent.
    virtual void publish_part_geometry(std::span<PartGeometry> geometry) const;
    // The geometry publish_part_geometry produces for the dynamic type.
    [[nodiscard]] PartGeometryBuffer published_part_geometry() const;
    // Called by realized_item_system with `item` in range. A type with more
    // than one item system overrides it.
    [[nodiscard]] virtual ItemSystemIndex publish_realized_item_system(std::size_t item) const;
    // Called by fill_realized_item_geometry with `geometry` sized to the
    // parts of the item's system, every record absent, and `item` in range.
    virtual void publish_realized_item_geometry(std::size_t item,
                                                std::span<PartGeometry> geometry) const;
    [[nodiscard]] PartGeometryBuffer published_realized_item_geometry(
        std::size_t item) const;
    // The base states every control shares: normal, enabled or disabled,
    // focused and focus-visible. Overrides start from this and add their own.
    [[nodiscard]] AnatomyStateSet base_anatomy_states() const;
    [[nodiscard]] Window* window() const noexcept { return window_; }
    // The surface root whose control table holds this control; null while
    // detached. Set and cleared together with window().
    [[nodiscard]] SurfaceRoot* surface_root() const noexcept { return surface_root_; }
    // Chooses where this type's tooltip shows (ToolTipPresentation); bubble
    // by default. Call it from the constructor, or when the control changes
    // what it is (a ComboBox gaining or losing its edit field).
    void set_tool_tip_presentation(ToolTipPresentation presentation) noexcept {
        tool_tip_presentation_ = presentation;
    }
    // The hint a typed-text field paints in light grey while it has focus,
    // is empty and has no placeholder: its own tooltip text, else that of
    // the nearest enclosing field_hint control it edits for (a
    // NumericUpDown's or an editable ComboBox's TextBox). Empty when this
    // control is not a field_hint, or no such text is set.
    [[nodiscard]] std::string field_hint_text() const;
    // Declares that this type's semantic virtual children may carry part
    // tooltips (publishes_item_tool_tips). Call it from the constructor.
    void set_publishes_item_tool_tips(bool publishes) noexcept {
        publishes_item_tool_tips_ = publishes;
    }
    void require_mutable() const;
    // Layout is allowed to invoke application-overridable measurement. Such a
    // callback may legally mutate the retained tree, so layout authors must
    // iterate a strong identity snapshot and revalidate each identity before
    // publishing geometry. Children added during a callback are deliberately
    // picked up by the next bounded layout pass.
    [[nodiscard]] std::vector<Ptr> snapshot_layout_children() const;
    [[nodiscard]] bool is_current_layout_child(const Ptr& child) const noexcept;
    // Publish a parent-owned arranged slot without destroying the child's
    // authored/requested bounds. Every retained layout family uses this seam;
    // it is protected because application absolute positioning is expressed
    // through requested bounds or a public layout control.
    void set_child_layout(const Ptr& child, Rect bounds);
    void set_child_layout_collapsed(const Ptr& child, bool collapsed);
    // Containing blocks of absolutely positioned children (stage L, phase
    // L5; PLAN sections 2.5 and 3.3). A container reports each such child's
    // resolution (the window records containing_block_not_parent once per
    // change), registers the controls whose arrangement moves its children's
    // containing block (an empty list withdraws it), and reports a direct
    // child whose positioning changed, so that the containers that resolved
    // through it arrange again. Each does nothing while detached.
    void note_containing_block(const Control& child, const Control& ancestor);
    void set_containing_block_dependencies(std::span<const RuntimeId> watched);
    void notify_positioning_changed(const Control& child);
    // Custom retained layout controls establish their own child slots after
    // arranging only themselves. Calling the public base arrange would also
    // run the default Dock/Anchor engine and create competing layout owners.
    void arrange_self(Rect final_bounds) noexcept;
    // Framework chrome is recorded after application paint. Descendant replay
    // and hit testing are clipped to the local child viewport, letting a
    // retained scroll container reserve its bars without a platform HWND.
    virtual void on_paint_overlay(Painter& painter, Rect local_damage);
    [[nodiscard]] virtual Rect child_viewport_rectangle() const noexcept {
        return client_rectangle();
    }
    [[nodiscard]] bool prepare_command_activation();
    [[nodiscard]] bool focus_next_after_self();
    [[nodiscard]] virtual bool perform_dialog_command();
    [[nodiscard]] virtual bool supports_dialog_command() const noexcept;
    [[nodiscard]] virtual DialogResult command_dialog_result() const noexcept;
    virtual void assign_cancel_dialog_result();
    [[nodiscard]] virtual bool mnemonic_matches(
        char32_t character) const noexcept;
    virtual bool process_mnemonic_self(char32_t character);
    virtual void notify_default(bool value);
    void define_bindable_property(BindableProperty property);
    // Property/state notifications are synchronous outside initialization.
    // During a nested BeginInit/EndInit scope, one latest-value publication is
    // retained per event and released only after the outer scope has committed
    // its accumulated invalidation. This prevents callbacks from observing or
    // re-entering a partially initialized native control.
    template <typename... EventArguments, typename... Values>
    void publish_change(Event<EventArguments...>& event, Values&&... values) {
        using Publication = detail::DeferredEventPublication<
            Event<EventArguments...>, std::decay_t<Values>...>;
        Publication publication(event,
            std::decay_t<Values>(std::forward<Values>(values))...);
        if (initialization_depth_ == 0U) {
            // Keep the same argument snapshot without wrapping a synchronous
            // publication in std::function. Its small-buffer policy differs
            // between standard libraries and may allocate even for scalars.
            publication();
            return;
        }
        publish_change(static_cast<const void*>(&event), std::move(publication));
    }
    // Language/object adapters may use Control solely as the retained lifetime
    // and PropertyGrid owner. They clear the stock visual schema before
    // defining the foreign object's own inert descriptors.
    void clear_bindable_properties();
    virtual void on_attached_to_window();
    virtual void on_attachment_committed() noexcept;
    // Last noexcept callback while the former Window is still available.
    // Controls use it to retire window-owned resources before detachment.
    virtual void on_detaching_from_window(SurfaceRoot& former_window) noexcept;
    virtual void on_detached_from_window() noexcept;
    // Derived disposal hooks that retain child controls must finish through
    // this base implementation; it owns the mutation-free child detach path.
    void on_dispose() noexcept override;

private:
    // The theme the control takes without its own override: its parent's,
    // else its window's, else the default theme.
    [[nodiscard]] const Theme& inherited_theme() const noexcept;
    // The theme the control was given, before any fallback: the nearest
    // override, else the window's application theme, else the default.
    [[nodiscard]] const Theme& chosen_theme() const noexcept;
    // Writes W404 for `property` of this control.
    void warn_inline_colour(std::string_view property) const;
    // W405 clear_control_paints_face, once per control: a control declared
    // clear painted a full-face fill of its own.
    void warn_clear_face() const noexcept;
    // After the control's classes or inline style change: drops the cached
    // primary font, and returns Dirty::layout where it resolves differently
    // now (a font is a layout input), else Dirty::none.
    [[nodiscard]] Dirty refresh_text_font() noexcept;
    // P12b: compiles again, against `theme`, each inline style of this
    // subtree that defines custom properties and was compiled against
    // another theme, appending each to `restyles` uninstalled; a control
    // below this one with an override of its own keeps its theme and is
    // passed over with its subtree. False, the diagnostics appended, at the
    // first that does not compile.
    [[nodiscard]] bool collect_inline_restyles(const Theme& theme, std::vector<detail::InlineRestyle>& restyles,
                                               std::vector<ThemeCompileDiagnostic>& diagnostics);
    // Installs restyles collected by collect_inline_restyles, with no user
    // code run between (their controls are alive).
    static void commit_inline_restyles(std::vector<detail::InlineRestyle>& restyles);
    // On attachment to a window: an inline style defining custom
    // properties compiled against another theme than the one now in effect
    // is compiled again. One that no longer compiles is kept as it was,
    // with one line written to stderr naming the control and the first
    // error: the theme's rules reading its names then keep the theme's
    // definitions (CompiledThemeRules::theme_serial differs).
    void refresh_inline_style();
    friend class Window;
    friend class SurfaceRoot;
    // The property capabilities (study section 14) reach the hooks below.
    friend struct NameCapability<Control>;
    friend struct VisibleCapability<Control>;
    friend struct EnabledCapability<Control>;
    friend struct AutoSizeCapability<Control>;
    friend struct CausesValidationCapability<Control>;
    friend struct ToolTipTextCapability<Control>;
    friend struct CursorCapability<Control>;
    friend struct BackColorCapability<Control>;
    friend struct ForeColorCapability<Control>;
    friend struct MinimumSizeCapability<Control>;
    friend struct MaximumSizeCapability<Control>;
    friend struct MarginCapability<Control>;
    friend struct PaddingCapability<Control>;
    friend struct DockCapability<Control>;
    friend struct AnchorCapability<Control>;
    friend struct AutoSizeModeCapability<Control>;
    friend struct LayoutDirectionCapability<Control>;
    friend struct TabIndexCapability<Control>;
    friend struct TabStopCapability<Control>;
    friend struct AllowDropCapability<Control>;
    friend struct HitTestTransparentCapability<Control>;
    friend struct AccessibleNameCapability<Control>;
    friend struct AccessibleDescriptionCapability<Control>;
    // The capabilities' hooks, one pair per property, chosen by the cell's
    // type: prepare_write runs before the compare (require_mutable first,
    // then the value's checks; it may adjust the value), written after the
    // store (the touch, side effects, the change event through
    // publish_change, and the on_<property>_changed virtual where Windows
    // Forms has OnXChanged). A refused value throws from prepare_write and
    // changes nothing.
    void prepare_write(const decltype(name)& cell, std::string& value);
    void written(const decltype(name)& cell);
    void prepare_write(const decltype(visible)& cell, bool& value);
    void written(const decltype(visible)& cell);
    void prepare_write(const decltype(enabled)& cell, bool& value);
    void written(const decltype(enabled)& cell);
    void prepare_write(const decltype(auto_size)& cell, bool& value);
    void written(const decltype(auto_size)& cell);
    void prepare_write(const decltype(causes_validation)& cell, bool& value);
    void written(const decltype(causes_validation)& cell);
    void prepare_write(const decltype(tool_tip_text)& cell, std::string& value);
    void written(const decltype(tool_tip_text)& cell);
    void prepare_write(const decltype(cursor)& cell, std::optional<CursorKind>& value);
    void written(const decltype(cursor)& cell);
    void prepare_write(const decltype(back_color)& cell, std::optional<Color>& value);
    void written(const decltype(back_color)& cell);
    void prepare_write(const decltype(fore_color)& cell, std::optional<Color>& value);
    void written(const decltype(fore_color)& cell);
    void prepare_write(const decltype(minimum_size)& cell, Size& value);
    void written(const decltype(minimum_size)& cell);
    void prepare_write(const decltype(maximum_size)& cell, Size& value);
    void written(const decltype(maximum_size)& cell);
    void prepare_write(const decltype(margin)& cell, Insets& value);
    void written(const decltype(margin)& cell);
    void prepare_write(const decltype(padding)& cell, Insets& value);
    void written(const decltype(padding)& cell);
    void prepare_write(const decltype(dock)& cell, DockStyle& value);
    void written(const decltype(dock)& cell);
    void prepare_write(const decltype(anchor)& cell, AnchorStyles& value);
    void written(const decltype(anchor)& cell);
    void prepare_write(const decltype(auto_size_mode)& cell, AutoSizeMode& value);
    void written(const decltype(auto_size_mode)& cell);
    void prepare_write(const decltype(layout_direction)& cell, LayoutDirection& value);
    void written(const decltype(layout_direction)& cell);
    void prepare_write(const decltype(tab_index)& cell, std::uint32_t& value);
    void written(const decltype(tab_index)& cell);
    void prepare_write(const decltype(tab_stop)& cell, bool& value);
    void written(const decltype(tab_stop)& cell);
    void prepare_write(const decltype(allow_drop)& cell, bool& value);
    void written(const decltype(allow_drop)& cell);
    void prepare_write(const decltype(hit_test_transparent)& cell, bool& value);
    void written(const decltype(hit_test_transparent)& cell);
    void prepare_write(const decltype(accessible_name)& cell, std::string& value);
    void written(const decltype(accessible_name)& cell);
    void prepare_write(const decltype(accessible_description)& cell, std::string& value);
    void written(const decltype(accessible_description)& cell);
    friend class ContainerControl;
    // Reports an opaque engine face's cover (report_opaque_cover).
    friend void paint_part_with_built_in_recipe(Painter& painter, const Control& control, const PartPaint& part);
    friend class ErrorProvider;
    friend class HelpProvider;
    friend class Binding;
    friend class ControlBindingsCollection;

    void clear_dirty(Dirty dirty) noexcept;
    void clear_subtree_dirty(Dirty dirty) noexcept;
    // Adds `dirty` to this control's own flags and, when it carries
    // Dirty::measure, advances the measure generation. Every addition of
    // own dirtiness goes through here so the measure cache cannot outlive
    // the invalidation that made it stale.
    void add_dirty(Dirty dirty) noexcept;
    void advance_measure_generation() noexcept;
    void set_provider_error(std::uint64_t provider_id, std::string error);
    void clear_provider_error(std::uint64_t provider_id);
    void set_provider_help(std::uint64_t provider_id, std::string help);
    void clear_provider_help(std::uint64_t provider_id);
    [[nodiscard]] bool perform_validation(Control* destination, bool bulk);
    [[nodiscard]] virtual AutoValidate authored_auto_validate() const noexcept {
        return AutoValidate::inherit;
    }
    // The table entry named `registered` (a registered name), or null.
    // The instance registry's registration only (not the table's entries).
    [[nodiscard]] const BindableProperty* find_bindable_property(
        std::string_view name) const;
    [[nodiscard]] std::uint64_t subtree_size() const noexcept;
    [[nodiscard]] bool initialization_blocked() const noexcept;
    void verify_dispose_thread() override;
    void paint_authored_surface(Painter& painter, Rect bounds) const;
    // Fills `bounds` with this control's own background colour, unless it
    // has none or the window is in forced high contrast.
    void paint_back_color(Painter& painter, Rect bounds) const;
    void paint_owned_decorations(Painter& painter, Rect bounds,
                                 OwnerDecorationLayer layer) const;
    [[nodiscard]] Insets effective_visual_outsets() const noexcept;
    void validate_authored_surface_material_images(
        const SurfaceMaterial& material) const;
    void publish_change(const void* event_key,
                        std::function<void()> publication);
    void define_structural_property(PropertyDescriptor descriptor,
                                    PropertyRegistration::Getter get,
                                    PropertyRegistration::Setter set);
    [[nodiscard]] static bool tab_order_less(const Ptr& left,
                                             const Ptr& right) noexcept;
    static void collect_tab_order_controls(const Control& owner,
                                           std::vector<Ptr>& ordered);
    static void apply_subtree_dirty(Control& control, Dirty requested_dirty);
    static void collect_mnemonic_candidates(const Ptr& control,
                                            char32_t character,
                                            std::vector<Ptr>& candidates);
    [[nodiscard]] static std::shared_ptr<const PropertyEnumDescriptor>
        dock_style_property_enum();
    [[nodiscard]] static std::shared_ptr<const PropertyEnumDescriptor>
        anchor_styles_property_enum();
    [[nodiscard]] static std::shared_ptr<const PropertyEnumDescriptor>
        auto_size_mode_property_enum();
    [[nodiscard]] static std::shared_ptr<const PropertyEnumDescriptor>
        layout_direction_property_enum();
    // The cursor property: "inherit" (no cursor of its own) or a kind.
    [[nodiscard]] BindingValue cursor_property_value() const;
    void set_cursor_property_value(const BindingValue& value);
    // back_color and fore_color as nullable colour properties: null for no
    // colour of the control's own, and setting null clears it.
    [[nodiscard]] BindingValue back_color_property_value() const;
    void set_back_color_property_value(const BindingValue& value);
    [[nodiscard]] BindingValue fore_color_property_value() const;
    void set_fore_color_property_value(const BindingValue& value);
    [[nodiscard]] static BindingValue current_enum_property_value(
        std::shared_ptr<const PropertyEnumDescriptor> enumeration,
        std::int64_t value);

    struct DeferredInitializationChange final {
        const void* event_key{};
        std::function<void()> publication;
    };
    using ChildList = std::vector<Ptr>;
    using ProviderTextMap = std::map<std::uint64_t, std::string>;
    using BindablePropertyMap = std::map<std::string, BindableProperty>;
    using DeferredInitializationChangeList =
        std::vector<DeferredInitializationChange>;

    static std::atomic<std::uint64_t> next_runtime_id_;
    RuntimeId runtime_id_;
    StableId stable_id_;
    std::any tag_;
    WeakPtr parent_;
    ChildList children_;
    Window* window_{};
    SurfaceRoot* surface_root_{};
    // This control's slot in its surface root's control table while it is
    // attached (assigned and released with surface_root_); unassigned
    // otherwise.
    detail::ControlSlot surface_slot_{};
    // Atomic shared_ptr free functions make BeginInvoke safe to acquire from a
    // worker while the UI thread attaches or detaches this control.
    mutable std::mutex dispatcher_mutex_;
    std::shared_ptr<detail::DispatcherState> dispatcher_state_;
    Rect requested_bounds_{};
    Rect arranged_bounds_{};
    // A layout container assigns this slot without destroying the child's
    // authored/preferred requested bounds. Unmanaged children leave it empty.
    std::optional<Rect> layout_slot_;
    struct AnchorReference final {
        Rect bounds;
        Rect client;
    };
    std::optional<AnchorReference> anchor_reference_;
    // Drops the anchor reference and, when the control is undocked and it
    // and its attached parent are arranged, captures the current one.
    void recapture_anchor_reference();
    Point auto_scroll_offset_{};
    // Whether the arrange the pass last called reached Control::arrange:
    // cleared by the arrange pass before it calls arrange, set by
    // Control::arrange (layout_resize_response's default).
    bool arranged_by_dock_layout_{};
    Dirty dirty_{Dirty::layout | Dirty::paint | Dirty::hit_test | Dirty::semantics};
    Dirty subtree_dirty_{Dirty::layout | Dirty::paint | Dirty::hit_test |
                         Dirty::semantics};
    PaintPlane paint_plane_{PaintPlane::control};
    std::shared_ptr<const detail::DisplayChunk> display_chunk_;
    Insets last_painted_visual_outsets_{};
    bool layout_collapsed_{};
    bool focusable_{};
    // Whether this is its window's hovered control, the one the pointer was
    // last over (stage T4, hot states). Only the window sets it, before the
    // control sees the enter or leave; base_anatomy_states reports it as
    // hot.
    bool pointer_over_{};
    // resize_redraw is on by default (unlike Windows Forms): a control
    // repaints all of itself when its size changes unless it, or its
    // application, clears it, or it overrides paint_depends_on_size.
    ControlStyles styles_{ControlStyles::user_paint |
                          ControlStyles::resize_redraw |
                          ControlStyles::standard_click |
                          ControlStyles::selectable |
                          ControlStyles::all_painting_in_one_pass};
    std::vector<std::string> classes_;
    std::shared_ptr<const detail::ControlInlineStyle> inline_style_;
    bool ceremonial_{};
    CursorImagesPtr cursor_images_;
    std::shared_ptr<const Theme> theme_override_;
    std::optional<SurfaceMaterial> authored_surface_material_;
    bool back_color_warned_{};
    bool fore_color_warned_{};
    // The primary text font last resolved (text_font), and what it was
    // resolved against: valid while the theme, the window's media facts,
    // the user theme's and the inline style's rules and `own` are the same.
    // The classes are not compared: set_classes drops it (refresh_text_font).
    struct TextFontCache final {
        bool valid{};
        FontSpec own{};
        const Theme* theme{};
        MediaFacts media{};
        const CompiledThemeRules* user_rules{};
        const CompiledThemeRules* inline_rules{};
        ResolvedFont resolved{};
    };
    mutable TextFontCache text_font_cache_{};
    // Whether the theme declares the control clear (declares_clarity), as
    // last resolved, and what it was resolved against.
    struct ClarityCache final {
        bool valid{};
        const Theme* theme{};
        MediaFacts media{};
        const CompiledThemeRules* user_rules{};
        const CompiledThemeRules* inline_rules{};
        bool clear{};
    };
    mutable ClarityCache clarity_cache_{};
    mutable bool clear_face_warned_{};
    bool font_from_code_{};
    std::vector<OwnerDecorationRecipe> owned_decorations_;
    ControlVisualStatus visual_status_{ControlVisualStatus::normal};
    ToolTipPresentation tool_tip_presentation_{ToolTipPresentation::bubble};
    bool publishes_item_tool_tips_{};
    std::shared_ptr<CommandTarget> command_target_;
    // Ordered by provider identity so semantic traces remain byte-for-byte
    // deterministic when multiple nonvisual providers extend one control.
    ProviderTextMap provider_errors_;
    ProviderTextMap provider_help_;
    Event<Dirty, bool> initialization_completed_;
    Event<const std::string&> name_changed_;
    Event<const std::string&> tool_tip_text_changed_;
    Event<bool> visible_changed_;
    Event<bool> enabled_changed_;
    Event<bool> causes_validation_changed_;
    Event<bool> auto_size_changed_;
    Event<ControlValidationEvent&> validating_;
    Event<> validated_;
    Event<const PointerEvent&> pointer_observed_;
    Event<const PointerEvent&> pointer_preview_observed_;
    Event<bool> focus_observed_;
    Event<Rect> arranged_bounds_changed_;
    Event<HelpRequestEvent&> help_requested_;
    BindablePropertyMap bindable_properties_;
    mutable std::unique_ptr<ControlBindingsCollection> data_bindings_;
    Dirty pending_initialization_dirty_{Dirty::none};
    std::uint64_t initialization_depth_{};
    bool pending_initialization_subtree_{};
    DeferredInitializationChangeList pending_initialization_changes_;
    bool lifecycle_notification_{};
    // One stored measure_for_layout answer. A generation of zero is never
    // current, so a value-initialized entry is empty.
    struct MeasureCacheEntry final {
        LayoutConstraint constraint{};
        Size result{};
        std::uint64_t generation{};
    };
    // Slots: min-content width, max-content width, and two for every other
    // query, replaced oldest first.
    static constexpr std::size_t measure_cache_capacity = 4U;
    std::array<MeasureCacheEntry, measure_cache_capacity> measure_cache_{};
    std::size_t measure_cache_next_general_slot_{2U};
    std::uint64_t measure_generation_{1U};
    // The window layout pass in which this control last answered a miss,
    // and how many misses it answered in that pass.
    std::uint64_t measure_pass_sequence_{};
    std::uint64_t measure_pass_callbacks_{};
    std::uint32_t layout_suspend_depth_{};
    bool layout_deferred_{};
    std::uint64_t layout_requested_revision_{};
    std::uint64_t layout_committed_revision_{};

    enum class RegisteredProperty : std::uint8_t {
        name,
        visible,
        enabled,
        auto_size,
        causes_validation,
        bounds,
        minimum_size,
        maximum_size,
        margin,
        padding,
        auto_scroll_offset,
        dock,
        anchor,
        auto_size_mode,
        layout_direction,
        tab_index,
        tab_stop,
        allow_drop,
        hit_test_transparent,
        accessible_name,
        accessible_description,
        tool_tip_text,
        cursor,
        back_color,
        fore_color,
    };

    // Control's table operations over the adapters below (study section
    // 14): one entry per migrated property, built by
    // control_property_entries.
    [[nodiscard]] static PropertyDescriptor registered_descriptor(RegisteredProperty property);
    template <RegisteredProperty Property>
    [[nodiscard]] static const PropertyDescriptor& describe_registered();
    template <RegisteredProperty Property>
    [[nodiscard]] static BindingValue read_registered(const Component& owner);
    template <RegisteredProperty Property>
    static void write_registered(Component& owner, const BindingValue& value);
    template <RegisteredProperty Property>
    static void reset_registered(Component& owner);
    template <RegisteredProperty Property>
    [[nodiscard]] static SubscriptionToken connect_registered(Component& owner, Component& subscriber,
                                                              std::function<void()> changed);

    template <RegisteredProperty Property>
    struct RegisteredPropertyGetter final {
        const Control* control{};

        BindingValue operator()() const {
            if constexpr (Property == RegisteredProperty::name) {
                return BindingValue{(*control).name.read()};
            } else if constexpr (Property == RegisteredProperty::visible) {
                return BindingValue{(*control).visible.read()};
            } else if constexpr (Property == RegisteredProperty::enabled) {
                return BindingValue{(*control).enabled.read()};
            } else if constexpr (Property == RegisteredProperty::auto_size) {
                return BindingValue{(*control).auto_size.read()};
            } else if constexpr (
                Property == RegisteredProperty::causes_validation) {
                return BindingValue{(*control).causes_validation.read()};
            } else if constexpr (Property == RegisteredProperty::bounds) {
                return BindingValue{(*control).requested_bounds_};
            } else if constexpr (
                Property == RegisteredProperty::minimum_size) {
                return BindingValue{(*control).minimum_size.read()};
            } else if constexpr (
                Property == RegisteredProperty::maximum_size) {
                return BindingValue{(*control).maximum_size.read()};
            } else if constexpr (Property == RegisteredProperty::margin) {
                return BindingValue{(*control).margin.read()};
            } else if constexpr (Property == RegisteredProperty::padding) {
                return BindingValue{(*control).padding.read()};
            } else if constexpr (
                Property == RegisteredProperty::auto_scroll_offset) {
                return BindingValue{(*control).auto_scroll_offset_};
            } else if constexpr (Property == RegisteredProperty::dock) {
                return Control::current_enum_property_value(
                    Control::dock_style_property_enum(),
                    static_cast<std::int64_t>((*control).dock.read()));
            } else if constexpr (Property == RegisteredProperty::anchor) {
                return Control::current_enum_property_value(
                    Control::anchor_styles_property_enum(),
                    static_cast<std::int64_t>((*control).anchor.read()));
            } else if constexpr (
                Property == RegisteredProperty::auto_size_mode) {
                return Control::current_enum_property_value(
                    Control::auto_size_mode_property_enum(),
                    static_cast<std::int64_t>((*control).auto_size_mode.read()));
            } else if constexpr (
                Property == RegisteredProperty::layout_direction) {
                return Control::current_enum_property_value(
                    Control::layout_direction_property_enum(),
                    static_cast<std::int64_t>((*control).layout_direction.read()));
            } else if constexpr (Property == RegisteredProperty::tab_index) {
                return BindingValue{static_cast<std::uint64_t>(
                    (*control).tab_index.read())};
            } else if constexpr (Property == RegisteredProperty::tab_stop) {
                return BindingValue{(*control).tab_stop.read()};
            } else if constexpr (Property == RegisteredProperty::allow_drop) {
                return BindingValue{(*control).allow_drop.read()};
            } else if constexpr (
                Property == RegisteredProperty::hit_test_transparent) {
                return BindingValue{(*control).hit_test_transparent.read()};
            } else if constexpr (
                Property == RegisteredProperty::accessible_name) {
                return BindingValue{(*control).accessible_name.read()};
            } else if constexpr (
                Property == RegisteredProperty::tool_tip_text) {
                return BindingValue{(*control).tool_tip_text.read()};
            } else if constexpr (Property == RegisteredProperty::cursor) {
                return (*control).cursor_property_value();
            } else if constexpr (Property == RegisteredProperty::back_color) {
                return (*control).back_color_property_value();
            } else if constexpr (Property == RegisteredProperty::fore_color) {
                return (*control).fore_color_property_value();
            } else {
                static_assert(
                    Property == RegisteredProperty::accessible_description);
                return BindingValue{(*control).accessible_description.read()};
            }
        }
    };

    template <RegisteredProperty Property>
    struct RegisteredPropertySetter final {
        Control* control{};

        void operator()(const BindingValue& value) const {
            if constexpr (Property == RegisteredProperty::name) {
                const std::optional<BindingValue> converted =
                    convert_binding_value(value, BindingValueKind::text);
                if (!converted) {
                    throw std::invalid_argument("Name binding requires text");
                }
                (*control).name = std::get<std::string>(*converted);
            } else if constexpr (Property == RegisteredProperty::visible) {
                const std::optional<BindingValue> converted =
                    convert_binding_value(value, BindingValueKind::boolean);
                if (!converted) {
                    throw std::invalid_argument(
                        "Visible binding requires Boolean");
                }
                (*control).visible = std::get<bool>(*converted);
            } else if constexpr (Property == RegisteredProperty::enabled) {
                const std::optional<BindingValue> converted =
                    convert_binding_value(value, BindingValueKind::boolean);
                if (!converted) {
                    throw std::invalid_argument(
                        "Enabled binding requires Boolean");
                }
                (*control).enabled = std::get<bool>(*converted);
            } else if constexpr (Property == RegisteredProperty::auto_size) {
                (*control).auto_size = std::get<bool>(value);
            } else if constexpr (
                Property == RegisteredProperty::causes_validation) {
                (*control).causes_validation = std::get<bool>(value);
            } else if constexpr (Property == RegisteredProperty::bounds) {
                (*control).set_requested_bounds(std::get<Rect>(value));
            } else if constexpr (
                Property == RegisteredProperty::minimum_size) {
                (*control).minimum_size = std::get<Size>(value);
            } else if constexpr (
                Property == RegisteredProperty::maximum_size) {
                (*control).maximum_size = std::get<Size>(value);
            } else if constexpr (Property == RegisteredProperty::margin) {
                (*control).margin = std::get<Insets>(value);
            } else if constexpr (Property == RegisteredProperty::padding) {
                (*control).padding = std::get<Insets>(value);
            } else if constexpr (
                Property == RegisteredProperty::auto_scroll_offset) {
                (*control).set_auto_scroll_offset(std::get<Point>(value));
            } else if constexpr (Property == RegisteredProperty::dock) {
                (*control).dock = static_cast<DockStyle>(
                    std::get<PropertyEnumValue>(value).value);
            } else if constexpr (Property == RegisteredProperty::anchor) {
                (*control).anchor = static_cast<AnchorStyles>(
                    std::get<PropertyEnumValue>(value).value);
            } else if constexpr (
                Property == RegisteredProperty::auto_size_mode) {
                (*control).auto_size_mode = static_cast<AutoSizeMode>(
                    std::get<PropertyEnumValue>(value).value);
            } else if constexpr (
                Property == RegisteredProperty::layout_direction) {
                (*control).layout_direction = static_cast<LayoutDirection>(
                    std::get<PropertyEnumValue>(value).value);
            } else if constexpr (Property == RegisteredProperty::tab_index) {
                const std::uint64_t index = std::get<std::uint64_t>(value);
                if (index > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::out_of_range(
                        "GUI.Forms TabIndex exceeds UInt32");
                }
                (*control).tab_index = static_cast<std::uint32_t>(index);
            } else if constexpr (Property == RegisteredProperty::tab_stop) {
                (*control).tab_stop = std::get<bool>(value);
            } else if constexpr (Property == RegisteredProperty::allow_drop) {
                (*control).allow_drop = std::get<bool>(value);
            } else if constexpr (
                Property == RegisteredProperty::hit_test_transparent) {
                (*control).hit_test_transparent = std::get<bool>(value);
            } else if constexpr (
                Property == RegisteredProperty::accessible_name) {
                (*control).accessible_name = std::get<std::string>(value);
            } else if constexpr (
                Property == RegisteredProperty::tool_tip_text) {
                const std::optional<BindingValue> converted =
                    convert_binding_value(value, BindingValueKind::text);
                if (!converted) {
                    throw std::invalid_argument(
                        "ToolTipText binding requires text");
                }
                (*control).tool_tip_text = std::get<std::string>(*converted);
            } else if constexpr (Property == RegisteredProperty::cursor) {
                (*control).set_cursor_property_value(value);
            } else if constexpr (Property == RegisteredProperty::back_color) {
                (*control).set_back_color_property_value(value);
            } else if constexpr (Property == RegisteredProperty::fore_color) {
                (*control).set_fore_color_property_value(value);
            } else {
                static_assert(
                    Property == RegisteredProperty::accessible_description);
                (*control).accessible_description = std::get<std::string>(value);
            }
        }
    };

    struct RegisteredPropertyChangeRelay final {
        std::function<void()> changed;

        template <typename Value>
        void operator()(const Value&) const {
            changed();
        }
    };

    template <RegisteredProperty Property>
    struct RegisteredPropertyConnector final {
        Control* control{};

        SubscriptionToken operator()(Component& owner,
                                     std::function<void()> changed) const {
            RegisteredPropertyChangeRelay relay{std::move(changed)};
            if constexpr (Property == RegisteredProperty::name) {
                return (*control).name_changed_.subscribe(owner,
                                                          std::move(relay));
            } else if constexpr (Property == RegisteredProperty::visible) {
                return (*control).visible_changed_.subscribe(
                    owner, std::move(relay));
            } else if constexpr (Property == RegisteredProperty::enabled) {
                return (*control).enabled_changed_.subscribe(
                    owner, std::move(relay));
            } else if constexpr (Property == RegisteredProperty::auto_size) {
                return (*control).auto_size_changed_.subscribe(
                    owner, std::move(relay));
            } else if constexpr (
                Property == RegisteredProperty::tool_tip_text) {
                return (*control).tool_tip_text_changed_.subscribe(
                    owner, std::move(relay));
            } else {
                static_assert(
                    Property == RegisteredProperty::causes_validation);
                return (*control).causes_validation_changed_.subscribe(
                    owner, std::move(relay));
            }
        }
    };
};

template <typename ControlType, typename... Arguments>
[[nodiscard]] std::shared_ptr<ControlType> make_control(StableId stable_id,
                                                        Arguments&&... arguments) {
    static_assert(std::is_base_of_v<Control, ControlType>);
    std::shared_ptr<ControlType> control = std::make_shared<ControlType>(
        std::move(stable_id), std::forward<Arguments>(arguments)...);
    // Retained compound controls cannot safely establish parent links from
    // their constructor because Control::add_child intentionally requires a
    // live shared owner for cycle checks. A type may opt into this bounded
    // post-construction step; ordinary leaf controls pay no runtime cost.
    if constexpr (ControlType::initialize_tree_after_construction) {
        (*control).initialize_control_tree();
    }
    return control;
}

} // namespace gui_forms
