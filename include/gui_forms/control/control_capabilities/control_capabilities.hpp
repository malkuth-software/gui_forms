#pragma once

#include "gui_forms/binding/property_cell/property_cell.hpp"
#include "gui_forms/control/control_layout_styles/control_layout_styles.hpp"
#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/types.hpp"
#include "gui_forms/types/cursor_image/cursor_image.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace gui_forms {

// Control's property capabilities (property-capabilities study, sections 2
// and 14; migration step M2). Each is a standard-layout struct whose only
// member is its property's cell, taken by the declaring class `Declaring`
// (Control) as a base. A write is
//
//   (*control).visible = false;
//
// and runs `assign`: the declaring class's prepare hook (its first statement
// is require_mutable, so a write from another thread throws even when the
// value is equal; then the value's checks and, where C# lets a subclass
// rewrite the value, the protected virtual coerce hook), the compare and
// store, then the written hook (the touch, side effects, and the change
// event through publish_change, ending in the protected virtual
// on_<property>_changed() where Windows Forms has OnXChanged). The hooks
// are private overloads on the declaring class chosen by the cell's type;
// the declaring class befriends each capability. A read is
// `(*control).visible.type()`.

// The stable authoring name (Control::Name).
template <typename Declaring>
struct NameCapability {
    PropertyCell<std::string, NameCapability<Declaring>> name{};

    static void assign(NameCapability& capability, std::string value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.name), value);
        if (!capability.name.replace(std::move(value))) return;
        control.written(std::as_const(capability.name));
    }
};

// Whether the control takes part in presentation and input (Visible).
template <typename Declaring>
struct VisibleCapability {
    PropertyCell<bool, VisibleCapability<Declaring>> visible{true};

    static void assign(VisibleCapability& capability, bool value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.visible), value);
        if (!capability.visible.replace(std::move(value))) return;
        control.written(std::as_const(capability.visible));
    }
};

// Whether the control receives ordinary user input (Enabled).
template <typename Declaring>
struct EnabledCapability {
    PropertyCell<bool, EnabledCapability<Declaring>> enabled{true};

    static void assign(EnabledCapability& capability, bool value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.enabled), value);
        if (!capability.enabled.replace(std::move(value))) return;
        control.written(std::as_const(capability.enabled));
    }
};

// Whether measurement determines the control's size (AutoSize).
template <typename Declaring>
struct AutoSizeCapability {
    PropertyCell<bool, AutoSizeCapability<Declaring>> auto_size{false};

    static void assign(AutoSizeCapability& capability, bool value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.auto_size), value);
        if (!capability.auto_size.replace(std::move(value))) return;
        control.written(std::as_const(capability.auto_size));
    }
};

// Whether leaving the control starts validation (CausesValidation).
template <typename Declaring>
struct CausesValidationCapability {
    PropertyCell<bool, CausesValidationCapability<Declaring>> causes_validation{true};

    static void assign(CausesValidationCapability& capability, bool value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.causes_validation), value);
        if (!capability.causes_validation.replace(std::move(value))) return;
        control.written(std::as_const(capability.causes_validation));
    }
};

// The plain tooltip text; empty never shows.
template <typename Declaring>
struct ToolTipTextCapability {
    GUI_FORMS_LOCALIZABLE(tool_tip) PropertyCell<std::string, ToolTipTextCapability<Declaring>> tool_tip_text{};

    static void assign(ToolTipTextCapability& capability, std::string value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.tool_tip_text), value);
        if (!capability.tool_tip_text.replace(std::move(value))) return;
        control.written(std::as_const(capability.tool_tip_text));
    }
};

// The pointer's cursor; empty takes the parent's (Cursor).
template <typename Declaring>
struct CursorCapability {
    PropertyCell<std::optional<CursorKind>, CursorCapability<Declaring>> cursor{};

    static void assign(CursorCapability& capability, std::optional<CursorKind> value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.cursor), value);
        if (!capability.cursor.replace(std::move(value))) return;
        control.written(std::as_const(capability.cursor));
    }
};

// The own background colour; empty takes the parent's.
template <typename Declaring>
struct BackColorCapability {
    PropertyCell<std::optional<Color>, BackColorCapability<Declaring>> back_color{};

    static void assign(BackColorCapability& capability, std::optional<Color> value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.back_color), value);
        if (!capability.back_color.replace(std::move(value))) return;
        control.written(std::as_const(capability.back_color));
    }
};

// The own text colour; empty takes the parent's.
template <typename Declaring>
struct ForeColorCapability {
    PropertyCell<std::optional<Color>, ForeColorCapability<Declaring>> fore_color{};

    static void assign(ForeColorCapability& capability, std::optional<Color> value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.fore_color), value);
        if (!capability.fore_color.replace(std::move(value))) return;
        control.written(std::as_const(capability.fore_color));
    }
};

// The smallest size layout gives the control (MinimumSize).
template <typename Declaring>
struct MinimumSizeCapability {
    PropertyCell<Size, MinimumSizeCapability<Declaring>> minimum_size{};

    static void assign(MinimumSizeCapability& capability, Size value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.minimum_size), value);
        if (!capability.minimum_size.replace(std::move(value))) return;
        control.written(std::as_const(capability.minimum_size));
    }
};

// The largest size layout gives the control; zero is unbounded.
template <typename Declaring>
struct MaximumSizeCapability {
    PropertyCell<Size, MaximumSizeCapability<Declaring>> maximum_size{};

    static void assign(MaximumSizeCapability& capability, Size value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.maximum_size), value);
        if (!capability.maximum_size.replace(std::move(value))) return;
        control.written(std::as_const(capability.maximum_size));
    }
};

// The space kept outside the control (Margin).
template <typename Declaring>
struct MarginCapability {
    PropertyCell<Insets, MarginCapability<Declaring>> margin{Insets{3.0, 3.0, 3.0, 3.0}};

    static void assign(MarginCapability& capability, Insets value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.margin), value);
        if (!capability.margin.replace(std::move(value))) return;
        control.written(std::as_const(capability.margin));
    }
};

// The space kept inside the control around its content (Padding).
template <typename Declaring>
struct PaddingCapability {
    PropertyCell<Insets, PaddingCapability<Declaring>> padding{};

    static void assign(PaddingCapability& capability, Insets value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.padding), value);
        if (!capability.padding.replace(std::move(value))) return;
        control.written(std::as_const(capability.padding));
    }
};

// The parent edge the control docks to (Dock).
template <typename Declaring>
struct DockCapability {
    PropertyCell<DockStyle, DockCapability<Declaring>> dock{DockStyle::none};

    static void assign(DockCapability& capability, DockStyle value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.dock), value);
        if (!capability.dock.replace(std::move(value))) return;
        control.written(std::as_const(capability.dock));
    }
};

// The parent edges the control keeps its distance to (Anchor).
template <typename Declaring>
struct AnchorCapability {
    PropertyCell<AnchorStyles, AnchorCapability<Declaring>> anchor{AnchorStyles::top | AnchorStyles::left};

    static void assign(AnchorCapability& capability, AnchorStyles value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.anchor), value);
        if (!capability.anchor.replace(std::move(value))) return;
        control.written(std::as_const(capability.anchor));
    }
};

// Whether auto size may shrink the control (AutoSizeMode).
template <typename Declaring>
struct AutoSizeModeCapability {
    PropertyCell<AutoSizeMode, AutoSizeModeCapability<Declaring>> auto_size_mode{AutoSizeMode::grow_only};

    static void assign(AutoSizeModeCapability& capability, AutoSizeMode value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.auto_size_mode), value);
        if (!capability.auto_size_mode.replace(std::move(value))) return;
        control.written(std::as_const(capability.auto_size_mode));
    }
};

// The control's own layout direction; inherit takes the parent's (RightToLeft).
template <typename Declaring>
struct LayoutDirectionCapability {
    PropertyCell<LayoutDirection, LayoutDirectionCapability<Declaring>> layout_direction{LayoutDirection::inherit};

    static void assign(LayoutDirectionCapability& capability, LayoutDirection value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.layout_direction), value);
        if (!capability.layout_direction.replace(std::move(value))) return;
        control.written(std::as_const(capability.layout_direction));
    }
};

// The control's place in its parent's tab order (TabIndex).
template <typename Declaring>
struct TabIndexCapability {
    PropertyCell<std::uint32_t, TabIndexCapability<Declaring>> tab_index{0U};

    static void assign(TabIndexCapability& capability, std::uint32_t value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.tab_index), value);
        if (!capability.tab_index.replace(std::move(value))) return;
        control.written(std::as_const(capability.tab_index));
    }
};

// Whether the Tab key stops at the control (TabStop).
template <typename Declaring>
struct TabStopCapability {
    PropertyCell<bool, TabStopCapability<Declaring>> tab_stop{true};

    static void assign(TabStopCapability& capability, bool value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.tab_stop), value);
        if (!capability.tab_stop.replace(std::move(value))) return;
        control.written(std::as_const(capability.tab_stop));
    }
};

// Whether the control accepts dropped data (AllowDrop).
template <typename Declaring>
struct AllowDropCapability {
    PropertyCell<bool, AllowDropCapability<Declaring>> allow_drop{false};

    static void assign(AllowDropCapability& capability, bool value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.allow_drop), value);
        if (!capability.allow_drop.replace(std::move(value))) return;
        control.written(std::as_const(capability.allow_drop));
    }
};

// Whether pointer hit testing passes through the control to what is under it.
template <typename Declaring>
struct HitTestTransparentCapability {
    PropertyCell<bool, HitTestTransparentCapability<Declaring>> hit_test_transparent{false};

    static void assign(HitTestTransparentCapability& capability, bool value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.hit_test_transparent), value);
        if (!capability.hit_test_transparent.replace(std::move(value))) return;
        control.written(std::as_const(capability.hit_test_transparent));
    }
};

// The name assistive technology announces.
template <typename Declaring>
struct AccessibleNameCapability {
    GUI_FORMS_LOCALIZABLE(accessible_name) PropertyCell<std::string, AccessibleNameCapability<Declaring>> accessible_name{};

    static void assign(AccessibleNameCapability& capability, std::string value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.accessible_name), value);
        if (!capability.accessible_name.replace(std::move(value))) return;
        control.written(std::as_const(capability.accessible_name));
    }
};

// The description assistive technology announces.
template <typename Declaring>
struct AccessibleDescriptionCapability {
    GUI_FORMS_LOCALIZABLE(accessible_description) PropertyCell<std::string, AccessibleDescriptionCapability<Declaring>> accessible_description{};

    static void assign(AccessibleDescriptionCapability& capability, std::string value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.accessible_description), value);
        if (!capability.accessible_description.replace(std::move(value))) return;
        control.written(std::as_const(capability.accessible_description));
    }
};

// Every Control capability, in the order Control lists them as bases.
template <typename Declaring>
struct ControlCapabilities : public NameCapability<Declaring>,
                             public VisibleCapability<Declaring>,
                             public EnabledCapability<Declaring>,
                             public AutoSizeCapability<Declaring>,
                             public CausesValidationCapability<Declaring>,
                             public ToolTipTextCapability<Declaring>,
                             public CursorCapability<Declaring>,
                             public BackColorCapability<Declaring>,
                             public ForeColorCapability<Declaring>,
                             public MinimumSizeCapability<Declaring>,
                             public MaximumSizeCapability<Declaring>,
                             public MarginCapability<Declaring>,
                             public PaddingCapability<Declaring>,
                             public DockCapability<Declaring>,
                             public AnchorCapability<Declaring>,
                             public AutoSizeModeCapability<Declaring>,
                             public LayoutDirectionCapability<Declaring>,
                             public TabIndexCapability<Declaring>,
                             public TabStopCapability<Declaring>,
                             public AllowDropCapability<Declaring>,
                             public HitTestTransparentCapability<Declaring>,
                             public AccessibleNameCapability<Declaring>,
                             public AccessibleDescriptionCapability<Declaring> {};

} // namespace gui_forms
