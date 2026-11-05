#!/usr/bin/env python3
"""Generate the admitted GUI.Forms/GUI.Drawing compatibility oversight ledger.

This intentionally consumes reference API lists rather than implementation
source.  The result is a planning inventory: a row marked partial is not a
claim that its WinForms behavior is complete.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
import re
import xml.etree.ElementTree as ET


# The official Windows Forms shipped API lists (dotnet/winforms) and
# reference packs (Microsoft.WindowsDesktop.App.Ref) the catalogue was last
# generated from. Recorded in the summary; regenerate when they move.
OFFICIAL_SOURCES = (
    ("8.0", "release/8.0", "edebee640e06f291a2a2e770cfb86d6bd42563f9", "8.0.32"),
    ("9.0", "release/9.0", "3e991f572c195fadf3b06e9e06c7c56d8ac4cd48", "9.0.21"),
    ("10.0", "release/10.0", "f3adc279ed152f9b88ec678218011168e90b0bdb", "10.0.13"),
)

# The catalogue before stage W regenerated it from the official surface.
PREVIOUS_FORMS_ROWS = 14575
PREVIOUS_FORMS_MISSING = 9850

# The C# compiler marks every ref struct [Obsolete] with this message for
# compilers that predate ref structs; it is not a deprecation.
REF_STRUCT_MESSAGE = "Types with embedded references are not supported"

FORM_EXCLUSIONS = {
    "activex": ("System.Windows.Forms.AxHost", "System.Windows.Forms.ActiveX"),
    "browser": ("System.Windows.Forms.WebBrowser", "System.Windows.Forms.Html"),
    "printing": (
        "System.Windows.Forms.PrintControllerWithStatusDialog",
        "System.Windows.Forms.PrintDialog",
        "System.Windows.Forms.PrintPreviewControl",
        "System.Windows.Forms.PrintPreviewDialog",
        "System.Windows.Forms.PageSetupDialog",
    ),
    "mdi": ("System.Windows.Forms.Mdi", "System.Windows.Forms.MDI"),
    "design_time": ("System.Windows.Forms.Design",),
    "classic_datagrid": (
        "System.Windows.Forms.DataGrid",
        "System.Windows.Forms.DataGridBoolColumn",
        "System.Windows.Forms.DataGridCell",
        "System.Windows.Forms.DataGridColumnStyle",
        "System.Windows.Forms.DataGridLineStyle",
        "System.Windows.Forms.DataGridParentRowsLabelStyle",
        "System.Windows.Forms.DataGridPreferredColumnWidthTypeConverter",
        "System.Windows.Forms.DataGridTableStyle",
        "System.Windows.Forms.DataGridTextBox",
        "System.Windows.Forms.DataGridTextBoxColumn",
    ),
    "obsolete_menu_toolbar": (
        "System.Windows.Forms.ContextMenu",
        "System.Windows.Forms.MainMenu",
        "System.Windows.Forms.MenuItem",
        "System.Windows.Forms.StatusBar",
        "System.Windows.Forms.ToolBar",
    ),
}

# Families excluded by the start of their type name, nested types included:
# a namespace-style prefix such as "System.Windows.Forms.Html" would not match
# HtmlElement or WebBrowserBase, which are siblings, not children.
FORM_EXCLUDED_STEMS = {
    "browser": ("Html", "WebBrowser"),
    "mdi": ("Mdi",),
}

FORM_PARTIAL_NATIVE = (
    "System.Windows.Forms.Application",
    "System.Windows.Forms.ApplicationContext",
    "System.Windows.Forms.Form",
    "System.Windows.Forms.Control",
    "System.Windows.Forms.ContainerControl",
    "System.Windows.Forms.UserControl",
    "System.Windows.Forms.Panel",
    "System.Windows.Forms.GroupBox",
    "System.Windows.Forms.FlowLayoutPanel",
    "System.Windows.Forms.TableLayoutPanel",
    "System.Windows.Forms.Label",
    "System.Windows.Forms.Button",
    "System.Windows.Forms.ButtonBase",
    "System.Windows.Forms.CheckBox",
    "System.Windows.Forms.RadioButton",
    "System.Windows.Forms.LinkLabel",
    "System.Windows.Forms.PictureBox",
    "System.Windows.Forms.ImageList",
    "System.Windows.Forms.TextBox",
    "System.Windows.Forms.TextBoxBase",
    "System.Windows.Forms.ComboBox",
    "System.Windows.Forms.ListBox",
    "System.Windows.Forms.CheckedListBox",
    "System.Windows.Forms.TreeView",
    "System.Windows.Forms.NumericUpDown",
    "System.Windows.Forms.TrackBar",
    "System.Windows.Forms.ProgressBar",
    "System.Windows.Forms.ScrollBar",
    "System.Windows.Forms.HScrollBar",
    "System.Windows.Forms.VScrollBar",
    "System.Windows.Forms.DateTimePicker",
    "System.Windows.Forms.SplitContainer",
    "System.Windows.Forms.SplitterPanel",
    "System.Windows.Forms.Splitter",
    "System.Windows.Forms.TabControl",
    "System.Windows.Forms.TabPage",
    "System.Windows.Forms.ToolTip",
    "System.Windows.Forms.ErrorProvider",
    "System.Windows.Forms.HelpProvider",
    "System.Windows.Forms.HelpEventArgs",
    "System.Windows.Forms.HelpEventHandler",
    "System.Windows.Forms.HelpNavigator",
    "System.Windows.Forms.ErrorBlinkStyle",
    "System.Windows.Forms.ErrorIconAlignment",
    "System.Windows.Forms.Binding",
    "System.Windows.Forms.BindingSource",
    "System.Windows.Forms.BindingManagerBase",
    "System.Windows.Forms.CurrencyManager",
    "System.Windows.Forms.BindingContext",
    "System.Windows.Forms.ControlBindingsCollection",
    "System.Windows.Forms.BindingCompleteEventArgs",
    "System.Windows.Forms.BindingCompleteContext",
    "System.Windows.Forms.BindingCompleteState",
    "System.Windows.Forms.DataSourceUpdateMode",
    "System.Windows.Forms.ControlUpdateMode",
    "System.Windows.Forms.AutoValidate",
    "System.Windows.Forms.ValidationConstraints",
    "System.Windows.Forms.DialogResult",
    "System.Windows.Forms.IButtonControl",
    "System.Windows.Forms.Timer",
    "System.Windows.Forms.ContextMenuStrip",
    "System.Windows.Forms.MenuStrip",
    "System.Windows.Forms.ToolStripMenuItem",
)

DRAWING_PARTIAL_NATIVE = (
    "System.Drawing.Bitmap",
    "System.Drawing.Brush",
    "System.Drawing.Color",
    "System.Drawing.Font",
    "System.Drawing.Graphics",
    "System.Drawing.Icon",
    "System.Drawing.Image",
    "System.Drawing.Pen",
    "System.Drawing.Point",
    "System.Drawing.Rectangle",
    "System.Drawing.Region",
    "System.Drawing.Size",
    "System.Drawing.SolidBrush",
    "System.Drawing.StringFormat",
    "System.Drawing.TextureBrush",
    "System.Drawing.Drawing2D.ColorBlend",
    "System.Drawing.Drawing2D.GraphicsPath",
    "System.Drawing.Drawing2D.HatchBrush",
    "System.Drawing.Drawing2D.LinearGradientBrush",
    "System.Drawing.Drawing2D.Matrix",
    "System.Drawing.Drawing2D.PathGradientBrush",
    "System.Drawing.Imaging.BitmapData",
    "System.Drawing.Imaging.ColorMap",
    "System.Drawing.Imaging.ColorMatrix",
    "System.Drawing.Imaging.ImageAttributes",
)

DRAWING_EXCLUSIONS = {
    "printing": ("System.Drawing.Printing",),
    "design_time": ("System.Drawing.Design",),
    "metafile": (
        "System.Drawing.Imaging.Metafile",
        "System.Drawing.Imaging.Emf",
        "System.Drawing.Imaging.Wmf",
    ),
}


def clean_api_line(line: str) -> tuple[str, str]:
    marker = ""
    line = line.strip()
    while line.startswith("["):
        end = line.find("]")
        if end < 0:
            break
        marker += line[: end + 1]
        line = line[end + 1 :]
    if line.startswith("~"):
        marker += "~"
        line = line[1:]
    for prefix in ("abstract ", "override ", "virtual ", "static readonly ", "static "):
        if line.startswith(prefix):
            marker += prefix.strip() + ";"
            line = line[len(prefix) :]
            break
    return line.strip(), marker


def owner_for(line: str, types: list[str]) -> str:
    candidates = [value for value in types if line == value or line.startswith(value + ".")]
    return max(candidates, key=len) if candidates else line.split("(", 1)[0].rsplit(".", 1)[0]


def form_family(owner: str) -> str:
    names = owner.split(".")
    leaf = names[-1]
    if any(word in owner for word in ("Accessible", "Automation")):
        return "accessibility_semantics"
    if any(word in leaf for word in ("Layout", "Panel", "Splitter", "Scroll")):
        return "layout_scrolling"
    if any(word in leaf for word in ("Text", "Mask", "RichText")):
        return "text_editing"
    if any(word in leaf for word in ("List", "Tree", "Grid", "Binding", "Currency")):
        return "collections_binding_virtualization"
    if any(word in leaf for word in ("Menu", "Strip", "ToolTip", "Help", "ErrorProvider")):
        return "commands_overlays_guidance"
    if any(word in leaf for word in ("Dialog", "ColorDialog", "FileDialog", "FolderBrowser")):
        return "dialogs_host_services"
    if any(word in leaf for word in ("Mouse", "Key", "Drag", "Clipboard", "Cursor", "DataObject")):
        return "input_transfer"
    if any(word in leaf for word in ("Renderer", "VisualStyle", "Paint", "Image", "Icon")):
        return "style_resources_owner_draw"
    if leaf in {"Application", "ApplicationContext", "Form", "Control", "ContainerControl", "UserControl"}:
        return "application_window_control_kernel"
    return "controls_components_misc"


def drawing_family(owner: str) -> str:
    if owner.startswith("System.Drawing.Drawing2D"):
        if any(word in owner for word in ("Brush", "Blend")):
            return "paint_gradients_textures"
        return "paths_transforms_regions_strokes"
    if owner.startswith("System.Drawing.Imaging"):
        return "images_pixels_color_adjustment"
    if owner.startswith("System.Drawing.Text"):
        return "fonts_text_glyphs"
    if any(word in owner for word in ("Font", "StringFormat", "CharacterRange")):
        return "fonts_text_glyphs"
    if any(word in owner for word in ("Image", "Bitmap", "Icon")):
        return "images_pixels_color_adjustment"
    if any(word in owner for word in ("Brush", "Pen")):
        return "paint_gradients_textures"
    if owner.endswith(("Point", "PointF", "Size", "SizeF", "Rectangle", "RectangleF", "Color")):
        return "values_geometry_color"
    if owner.endswith("Graphics"):
        return "graphics_state_commands_targets"
    return "drawing_misc"


def prefix_match(owner: str, prefixes: tuple[str, ...]) -> bool:
    return any(owner == prefix or owner.startswith(prefix + ".") for prefix in prefixes)


def forms_type_stem(owner: str) -> str:
    """The first type name under System.Windows.Forms in `owner`."""
    prefix = "System.Windows.Forms."
    if not owner.startswith(prefix):
        return ""
    return owner[len(prefix):].split(".", 1)[0]


def classify_forms(owner: str, member: str) -> tuple[str, str]:
    for reason, prefixes in FORM_EXCLUSIONS.items():
        if prefix_match(owner, prefixes):
            return "excluded", reason
    stem = forms_type_stem(owner)
    for reason, stems in FORM_EXCLUDED_STEMS.items():
        if stem.startswith(stems):
            return "excluded", reason
    if owner.startswith("System.Windows.Forms.ComponentModel.Com2Interop"):
        return "excluded", "com_interop"
    if owner in {"System.Windows.Forms.NativeWindow", "System.Windows.Forms.IWin32Window", "System.Windows.Forms.CreateParams", "System.Windows.Forms.Message"}:
        return "platform_extension_review", "portable core does not expose raw HWND/message policy"
    if owner == "System.Windows.Forms.Control" and member.startswith((
        "DoubleBuffered.",
        "GetStyle(",
        "SetStyle(",
        "OnPaintBackground(",
        "InvokePaintBackground(",
    )):
        return "measured_partial", "M11h-P1 generated facade reflects styles and physically proves coherent epoch-safe managed owner paint; overload and ordering parity remain"
    if owner == "System.Windows.Forms.Control" and (
        member.startswith("Invalidate(") or
        member == "Invalidated" or
        member.startswith("NotifyInvalidate(") or
        member.startswith("OnInvalidated(")
    ):
        return "measured_partial", "M11h-P1 physical Wine gate proves clipped rectangle/region invalidation, synchronous notification, child translation, damage merge, and failure restoration; nonrectangular fidelity and complete ordering remain"
    if owner in {
        "System.Windows.Forms.InvalidateEventArgs",
        "System.Windows.Forms.InvalidateEventHandler",
    }:
        return "measured_partial", "M11h-P1 generated nominal surface and physical Wine gate prove invalid rectangle construction, protected notification, and public event delivery"
    if owner in {
        "System.Windows.Forms.LayoutEventArgs",
        "System.Windows.Forms.LayoutEventHandler",
    }:
        return "measured_partial", "M12-P11 native transaction, ABI 0.21, and generated-facade gates prove affected component/control/property arguments, nested suspension, committed geometry, deferred resume, and bounded re-entry; exhaustive independent WinForms event ordering remains open"
    if owner == "System.Windows.Forms.Control" and (
        member == "Layout" or member.startswith((
            "OnLayout(",
            "PerformLayout(",
            "ResumeLayout(",
            "SuspendLayout(",
        ))
    ):
        return "measured_partial", "M12-P11 retained per-control transaction and ABI 0.21 preserve committed geometry under nested suspension, allow runnable siblings, carry exact managed layout arguments, defer ResumeLayout(false), and bound re-entrant passes; exhaustive independent WinForms event ordering remains open"
    if owner in {
        "System.Windows.Forms.AutoSizeMode",
        "System.Windows.Forms.BoundsSpecified",
        "System.Windows.Forms.GetChildAtPointSkip",
    }:
        return "measured_partial", "M12-P9 publishes the exact enum values and rejects undefined flag bits at retained and generated mutation boundaries"
    if owner == "System.Windows.Forms.Control" and member.startswith(
        "AutoScrollOffset."
    ):
        return "measured_partial", "M12-P10 retains per-control reveal offset through native C++, ABI 0.20, and the generated facade; nested dock/RTL oracle breadth remains open"
    if owner in {
        "System.Windows.Forms.ScrollProperties",
        "System.Windows.Forms.HScrollProperties",
        "System.Windows.Forms.VScrollProperties",
        "System.Windows.Forms.ScrollEventArgs",
        "System.Windows.Forms.ScrollEventHandler",
        "System.Windows.Forms.ScrollEventType",
        "System.Windows.Forms.ScrollOrientation",
        "const System.Windows.Forms.ScrollableControl",
    }:
        return "measured_partial", "M12-P10 native, C11 ABI 0.20, and generated-facade gates prove exact axis defaults/ranges/clamping, event vocabulary/arguments, state constants, ownership, and automatic/manual mutation; full independent WinForms oracle parity remains open"
    if owner == "System.Windows.Forms.ScrollableControl" and (
        member == "" or member.startswith((
            "AdjustFormScrollbars(",
            "AutoScroll.",
            "AutoScrollMargin.",
            "AutoScrollMinSize.",
            "AutoScrollPosition.",
            "DisplayRectangle.get",
            "GetScrollState(",
            "HorizontalScroll.get",
            "HScroll.",
            "OnLayout(",
            "OnMouseWheel(",
            "OnScroll(",
            "Scroll",
            "ScrollableControl(",
            "ScrollControlIntoView(",
            "ScrollToControl(",
            "SetAutoScrollMargin(",
            "SetDisplayRectLocation(",
            "SetScrollState(",
            "VerticalScroll.get",
            "VScroll.",
        ))
    ):
        return "measured_partial", "M12-P10 retained two-axis layout, ABI 0.20, and generated-facade gates prove non-destructive viewport projection, bar interdependence, manual axes, line/page/thumb input, nested wheel behavior, clipping, semantics, resize clamping, and control reveal; RTL, scaling, and exhaustive event-order oracle work remain"
    if owner == "System.Windows.Forms.Control" and member.startswith((
        "AutoSize.",
        "AutoSizeChanged",
        "Bounds.",
        "Bottom.get",
        "BringToFront(",
        "ClientRectangle.get",
        "ClientSize.",
        "Contains(",
        "ContainsFocus.get",
        "GetAutoSizeMode(",
        "GetChildAtPoint(",
        "GetNextControl(",
        "GetPreferredSize(",
        "Height.",
        "Left.",
        "Location.",
        "LocationChanged",
        "Margin.",
        "MaximumSize.",
        "MinimumSize.",
        "PointToClient(",
        "PointToScreen(",
        "PreferredSize.get",
        "RectangleToClient(",
        "RectangleToScreen(",
        "Right.get",
        "SendToBack(",
        "SetAutoSizeMode(",
        "SetBounds(",
        "SetBoundsCore(",
        "Size.",
        "SizeChanged",
        "TabIndex.",
        "TabStop.",
        "Top.",
        "Width.",
    )):
        return "measured_partial", "M12-P9 retained and generated gates prove constrained requested/client geometry, masked bounds, preferred/min/max sizing, GrowOnly/GrowAndShrink AutoSize, ancestry transforms, descendant containment, filtered direct-child lookup, nested TabIndex traversal, and coherent z order; portable host screen origin and complete protected event-order parity remain"
    if owner in {
        "System.Windows.Forms.Control.ControlCollection",
        "System.Windows.Forms.Control+ControlCollection",
    } and member.startswith((
        "Contains(",
        "GetChildIndex(",
        "IndexOf(",
        "SetChildIndex(",
    )):
        return "measured_partial", "M12-P9 native and generated gates prove direct membership plus topmost-first index/query/mutation semantics synchronized with layout, paint, and hit testing"
    if owner in {
        "System.Windows.Forms.ErrorBlinkStyle",
        "System.Windows.Forms.ErrorIconAlignment",
    }:
        return "measured_partial", "M12-P4 renderer-free provider gate proves retained enum policy, all published alignment positions, RTL mirroring, and bounded/continuous/disabled blink behavior"
    if owner == "System.Windows.Forms.ErrorProvider" and member.startswith((
        "BlinkRate.",
        "BlinkStyle.",
        "CanExtend(",
        "Clear(",
        "ContainerControl.get",
        "GetError(",
        "GetIconAlignment(",
        "GetIconPadding(",
        "HasErrors.get",
        "Icon.",
        "RightToLeft.",
        "RightToLeftChanged",
        "SetError(",
        "SetIconAlignment(",
        "SetIconPadding(",
        "Tag.",
    )):
        return "measured_partial", "M12-P4 retained ErrorProvider proves per-control metadata, overlay geometry, ImageId substitution, semantic invalid state, target/provider cleanup, occlusion/reduced-motion scheduling, and deterministic event behavior; data binding remains open"
    if owner == "System.Windows.Forms.HelpProvider" and member.startswith((
        "CanExtend(",
        "GetHelpKeyword(",
        "GetHelpNavigator(",
        "GetHelpString(",
        "GetShowHelp(",
        "HelpNamespace.",
        "ResetShowHelp(",
        "SetHelpKeyword(",
        "SetHelpNavigator(",
        "SetHelpString(",
        "SetShowHelp(",
        "Tag.",
    )):
        return "measured_partial", "M12-P4 renderer-free HelpProvider proves retained extender metadata, automatic/explicit ShowHelp state, semantic projection, and control-first/provider-second F1 routing without implicit external navigation"
    if owner in {
        "System.Windows.Forms.HelpEventArgs",
        "System.Windows.Forms.HelpEventHandler",
    }:
        return "measured_partial", "M12-P4 HelpRequestEvent carries target, position, namespace, string, keyword, navigator, keyboard origin, and mutable handled state through tokenized deterministic delivery"
    if owner == "System.Windows.Forms.Control" and member == "HelpRequested":
        return "measured_partial", "M12-P4 public tokenized Control help event is emitted before provider policy and can terminate routing by setting handled"
    if owner == "System.Windows.Forms.Control" and member.startswith("IsMnemonic("):
        return "measured_partial", "M12-P7 renderer-free mnemonic parser proves single-marker recognition, escaped ampersands, Unicode scalar retention, ASCII case folding, and display-text elision; locale-sensitive Unicode case folding and underline cue policy remain"
    if owner == "System.Windows.Forms.Control" and member.startswith("ProcessMnemonic("):
        return "measured_partial", "M12-P8 retained Control routing precomputes a stable eligible candidate set before callbacks, preventing disposal/reparent mutation from changing the arbitration walk; protected managed override identity remains"
    if owner == "System.Windows.Forms.DialogResult":
        return "measured_partial", "M12-P8 publishes and validates every exact WinForms DialogResult numeric value in the retained native core and generated facade; independent native modal-loop closure remains host projection work"
    if owner == "System.Windows.Forms.IButtonControl":
        return "measured_partial", "M12-P8 retained Button/Window behavior and the generated interface prove DialogResult get/set, default notification, validated PerformClick, and Click-before-result ordering; arbitrary third-party IButtonControl native adaptation remains"
    if owner == "System.Windows.Forms.ButtonBase" and member.startswith("UseMnemonic."):
        return "measured_partial", "M12-P7 retained ButtonBase proves opt-in marker parsing across measure, paint, semantics, and command routing"
    if owner == "System.Windows.Forms.Button" and member.startswith((
        "DialogResult.",
        "PerformClick(",
        "NotifyDefault(",
        "ProcessMnemonic(",
    )):
        return "measured_partial", "M12-P8 retained command gate proves programmatic click, default-cue transfer, mnemonic activation, availability rejection, validation cancellation, exact DialogResult validation, and Click-before-result propagation; independent native modal closure remains separate"
    if owner in {
        "System.Windows.Forms.CheckBox",
        "System.Windows.Forms.RadioButton",
        "System.Windows.Forms.GroupBox",
        "System.Windows.Forms.Label",
    } and member.startswith("ProcessMnemonic("):
        return "measured_partial", "M12-P8 retained mnemonic routing proves choice activation, Label/GroupBox next-control focus, and duplicate collision cycling over a precomputed stable candidate set; locale case folding and underline cue policy remain"
    if owner == "System.Windows.Forms.Label" and member.startswith("UseMnemonic."):
        return "measured_partial", "M12-P7 retained Label proves marker-aware measure, paint, semantics, and next-control focus"
    if owner == "System.Windows.Forms.ContainerControl" and member.startswith((
        "ProcessDialogChar(",
        "ProcessMnemonic(",
    )):
        return "measured_partial", "M12-P8 renderer-free retained subtree routing proves stable candidate snapshots, duplicate cycling, callback-mutation safety, effective visibility/enabled state, active-focus-scope containment, and counters; protected managed override identity remains"
    if owner == "System.Windows.Forms.Form" and member.startswith((
        "AcceptButton.",
        "CancelButton.",
        "DialogResult.",
        "ProcessDialogChar(",
        "ProcessDialogKey(",
        "ProcessMnemonic(",
    )):
        return "measured_partial", "M12-P8 Window/Form command kernel proves default/cancel assignment, cue transfer, Enter/Escape routing, exact retained DialogResult state, Click-before-result ordering, validation, disposal cleanup, active-scope containment, and counters; independent native modal closure and full managed protected-call projection remain"
    if owner in {
        "System.Windows.Forms.ToolStrip",
        "System.Windows.Forms.ToolStripDropDown",
        "System.Windows.Forms.ToolStripMenuItem",
        "System.Windows.Forms.ToolStripItem",
    } and member.startswith("ProcessMnemonic("):
        return "measured_partial", "M12-P8 retained MenuStrip and overlay rows prove marker-free presentation, top-level duplicate cycling, contained submenu mnemonic activation, and shared command execution; protected generated ToolStrip override identity and remaining item subclasses stay open"
    if owner == "System.Windows.Forms.HelpNavigator" and member != "TopicId = -2147483641":
        return "measured_partial", "M12-P4 retains six help navigation intents; TopicId and raw managed enum-value projection remain open"
    if owner in {
        "System.Windows.Forms.AutoValidate",
        "System.Windows.Forms.ValidationConstraints",
    }:
        return "measured_partial", "M12-P6 renderer-free validation gate proves the published enum values, inheritance, filtering, and prevent/allow focus branches"
    if owner == "System.Windows.Forms.Control" and member.startswith((
        "CausesValidation.",
        "CausesValidationChanged",
        "Validated",
        "Validating",
        "OnCausesValidationChanged(",
        "OnValidated(",
        "OnValidating(",
    )):
        return "measured_partial", "M12-P6 retained focus transaction proves CausesValidation, cancellable Validating, successful Validated, pre-focus-loss ordering, reentrancy rejection, and disposal-safe endpoint rechecks; managed delegate identity remains open"
    if owner == "System.Windows.Forms.ContainerControl" and member.startswith((
        "AutoValidate.",
        "AutoValidateChanged",
        "OnAutoValidateChanged(",
        "Validate(",
        "ValidateChildren(",
    )):
        return "measured_partial", "M12-P6 retained ContainerControl proves inherited AutoValidate, prevent/allow/disable policy, ancestor focus validation, explicit validation, and deterministic constrained child traversal; nested managed container edge parity remains"
    if owner in {
        "System.Windows.Forms.Form",
        "System.Windows.Forms.UserControl",
    } and member.startswith((
        "AutoValidate.",
        "AutoValidateChanged",
        "Validate(",
        "ValidateChildren(",
    )):
        return "measured_partial", "M12-P6 Form/UserControl inherit the measured retained ContainerControl validation transaction; independent native-form oracle and generated facade override identity remain"
    if owner == "System.Windows.Forms.ErrorProvider" and member.startswith((
        "BindToDataAndErrors(",
        "DataMember.",
        "DataSource.",
        "UpdateBinding(",
    )):
        return "measured_partial", "M12-P6 binding-aware ErrorProvider proves same-window BindingSource attachment, current-record field and aggregate errors, BindingComplete parse failures, currency refresh, aggregation, and synchronous source cleanup; arbitrary managed IDataErrorInfo and nested object traversal remain"
    if owner in {
        "System.Windows.Forms.BindingCompleteContext",
        "System.Windows.Forms.BindingCompleteState",
        "System.Windows.Forms.DataSourceUpdateMode",
        "System.Windows.Forms.ControlUpdateMode",
    }:
        return "measured_partial", "M12-P5 renderer-free and Wine gates prove the published enum values and update-mode branches"
    if owner == "System.Windows.Forms.Binding" and member.startswith((
        "BindableComponent.get",
        "Binding(",
        "BindingComplete",
        "Control.get",
        "ControlUpdateMode.",
        "DataSource.get",
        "DataSourceNullValue.",
        "DataSourceUpdateMode.",
        "Format",
        "FormatString.",
        "FormattingEnabled.",
        "IsBinding.get",
        "NullValue.",
        "Parse",
        "PropertyName.get",
        "ReadValue(",
        "WriteValue(",
        "OnBindingComplete(",
        "OnFormat(",
        "OnParse(",
    )):
        return "measured_partial", "M12-P5 retained Binding proves explicit descriptors, two-way modes, invariant format/parse, null substitution, post-commit completion, errors, reentrancy suppression, and endpoint cleanup; managed reflection, arbitrary IFormatProvider, and exact façade overloads remain"
    if owner == "System.Windows.Forms.BindingSource" and member.startswith((
        "Add(",
        "AllowEdit.get",
        "AllowNew.",
        "AllowRemove.get",
        "BindingComplete",
        "CancelEdit(",
        "Clear(",
        "Count.get",
        "CurrencyManager.get",
        "Current.get",
        "CurrentChanged",
        "CurrentItemChanged",
        "DataError",
        "DataMember.",
        "DataMemberChanged",
        "DataSourceChanged",
        "EndEdit(",
        "Find(",
        "Insert(",
        "IsBindingSuspended.get",
        "List.get",
        "ListChanged",
        "MoveFirst(",
        "MoveLast(",
        "MoveNext(",
        "MovePrevious(",
        "Position.",
        "PositionChanged",
        "RaiseListChangedEvents.",
        "RemoveAt(",
        "RemoveCurrent(",
        "ResetBindings(",
        "ResetCurrentItem(",
        "ResetItem(",
        "ResumeBinding(",
        "SuspendBinding(",
        "OnBindingComplete(",
        "OnCurrentChanged(",
        "OnCurrentItemChanged(",
        "OnDataError(",
        "OnDataMemberChanged(",
        "OnDataSourceChanged(",
        "OnListChanged(",
        "OnPositionChanged(",
    )):
        return "measured_partial", "M12-P5 retained record source proves currency, stable identity, add/insert/remove/edit, property/list/reset notification, suspension coalescing, completion/error propagation, deterministic ordering, and disposal; raw IList/object facades, nested DataMember, sort/filter, and descriptor reflection remain"
    if owner == "System.Windows.Forms.BindingManagerBase" and member.startswith((
        "BindingComplete",
        "CancelCurrentEdit(",
        "Count.get",
        "Current.get",
        "CurrentChanged",
        "CurrentItemChanged",
        "DataError",
        "EndCurrentEdit(",
        "IsBindingSuspended.get",
        "Position.",
        "PositionChanged",
        "PullData(",
        "PushData(",
        "ResumeBinding(",
        "SuspendBinding(",
    )):
        return "measured_partial", "M12-P5 CurrencyManager proves current/count/position, edit, suspension, manager-wide push/pull, and propagated completion over one retained BindingSource; arbitrary derived managers and descriptor collections remain"
    if owner == "System.Windows.Forms.CurrencyManager" and member.startswith((
        "CancelCurrentEdit(",
        "Count.get",
        "Current.get",
        "EndCurrentEdit(",
        "List.get",
        "ListChanged",
        "Position.",
        "Refresh(",
        "ResumeBinding(",
        "SuspendBinding(",
    )):
        return "measured_partial", "M12-P5 proves retained list currency, deterministic movement, edit transactions, reset refresh, and suspension; managed property descriptors remain"
    if owner == "System.Windows.Forms.BindingContext" and member.startswith((
        "Add(",
        "Clear(",
        "CollectionChanged",
        "Contains(",
        "Remove(",
        "this[",
        "AddCore(",
        "ClearCore(",
        "OnCollectionChanged(",
        "RemoveCore(",
    )):
        return "measured_partial", "M12-P5 same-window BindingContext proves manager identity, membership, ordered collection changes, and eager source-disposal removal; nested member paths and static context reassignment remain"
    if owner == "System.Windows.Forms.ControlBindingsCollection" and member.startswith((
        "Add(",
        "AddCore(",
        "BindableComponent.get",
        "Clear(",
        "ClearCore(",
        "Control.get",
        "ControlBindingsCollection(",
        "DefaultDataSourceUpdateMode.",
        "Remove(",
        "RemoveCore(",
        "this[",
    )):
        return "measured_partial", "M12-P5 per-Control ownership proves unique canonical properties, explicit/default update modes, add/find/remove/clear, and synchronous disposal; managed object/reflection overload and RemoveAt parity remain"
    if prefix_match(owner, FORM_PARTIAL_NATIVE):
        return "partial_native", "retained type/facade exists; this exact member remains unproven unless separately cited"
    return "missing", "admitted nominal base or unresolved package placement"


def classify_drawing(owner: str, member: str) -> tuple[str, str]:
    for reason, prefixes in DRAWING_EXCLUSIONS.items():
        if prefix_match(owner, prefixes):
            return "excluded", reason
    if "CopyFromScreen" in member:
        return "excluded", "arbitrary desktop capture"
    if "Metafile" in member:
        return "excluded", "metafile playback/recording"
    if any(word in member for word in ("GetHdc", "ReleaseHdc", "FromHdc", "FromHwnd", "GetHbitmap", "FromHbitmap", "FromHicon")):
        return "platform_extension_partial", "opaque native-surface lease only"
    if owner == "System.Drawing.Region" and member == "GetBounds":
        return "measured_partial", "M11h-P1 GUI.Drawing ABI region-bounds projection is physically consumed by Wine managed invalidation; graphics-transform and infinite-region parity remain"
    if prefix_match(owner, DRAWING_PARTIAL_NATIVE):
        return "partial_native", "GUI.Drawing type exists; exact overload/behavior remains ledger-open"
    if owner.startswith("System.Drawing.Imaging.ImageCodecInfo") or owner.startswith("System.Drawing.Imaging.PropertyItem"):
        return "package_review", "PNG-only core; other codecs/metadata require isolated admission"
    return "missing", "admitted GUI.Drawing oversight or unresolved package placement"


def version_key(version: str) -> tuple[int, ...]:
    return tuple(int(part) for part in version.split("."))


def member_name_and_count(member: str) -> tuple[str, int | None]:
    """The member's name and, for a call, its parameter count.

    `Scale(float factor)` is ("Scale", 1); `AutoScale.get` is ("AutoScale",
    None); an event or field is its name with None.
    """
    if "(" not in member:
        return member.split(".", 1)[0], None
    name, arguments = member.split("(", 1)
    arguments = arguments.rsplit(")", 1)[0].strip()
    if not arguments:
        return name, 0
    depth = 0
    count = 1
    for character in arguments:
        if character in "<[(":
            depth += 1
        elif character in ">])":
            depth -= 1
        elif character == "," and depth == 0:
            count += 1
    return name, count


def read_obsolete(path: Path | None) -> dict[str, list[dict[str, str]]]:
    """Obsolete entries by package major.minor version (read_obsolete.py output)."""
    entries: dict[str, list[dict[str, str]]] = {}
    if path is None:
        return entries
    with path.open(encoding="utf-8") as stream:
        for row in csv.DictReader(stream, dialect="excel-tab"):
            if row["message"].startswith(REF_STRUCT_MESSAGE):
                continue
            version = ".".join(row["version"].split(".")[:2])
            entries.setdefault(version, []).append(row)
    return entries


def obsolete_marker(owner: str, member: str, entries: list[dict[str, str]]) -> str:
    """The diagnostic of the entry making this type or member obsolete, or ""."""
    name, count = member_name_and_count(member) if member else ("", None)
    for entry in entries:
        diagnostic = entry["diagnostic"] or "obsolete"
        if entry["kind"] == "type":
            if owner == entry["owner"] or owner.startswith(entry["owner"] + "."):
                return diagnostic
            continue
        if not member or owner != entry["owner"] or name != entry["member"]:
            continue
        if entry["kind"] == "method":
            if count is not None and str(count) == entry["parameters"]:
                return diagnostic
            continue
        return diagnostic
    return ""


def read_forms(sources: list[str], obsolete: dict[str, list[dict[str, str]]]) -> list[dict[str, str]]:
    """Forms rows from shipped API lists given as VERSION=PATH.

    A row is one public API. The same API is spelled without nullability
    annotations in some lists and with them (`!`, `?`) in later ones, so lines
    are identified with the annotations removed; the row keeps the spelling of
    the latest list and records every version that lists it. The scope rule
    (owner, 2026-10-13) admits a row when some version of .NET 8.0 or later
    lists it without [Obsolete].
    """
    first_seen: dict[str, tuple[str, str, str]] = {}
    spelling: dict[str, str] = {}
    versions: dict[str, list[str]] = {}
    order: list[str] = []
    for source in sources:
        version, _, location = source.partition("=")
        path = Path(location)
        for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
            if not line.strip() or line.startswith("#") or line.startswith("*REMOVED*"):
                continue
            api, marker = clean_api_line(line)
            identity = re.sub(r"[!?]", "", api)
            if identity not in first_seen:
                first_seen[identity] = (f"{version}:{path.name}:{number}", marker, version)
                versions[identity] = []
                order.append(identity)
            spelling[identity] = api
            if version not in versions[identity]:
                versions[identity].append(version)
    types = sorted((spelling[identity] for identity in order if " -> " not in identity), key=len, reverse=True)
    rows = []
    for identity in order:
        api = spelling[identity]
        source_line, marker, _ = first_seen[identity]
        kind = "type" if " -> " not in api else "member"
        owner = api if kind == "type" else owner_for(api.split(" -> ", 1)[0], types)
        member = "" if kind == "type" else api[len(owner) + 1 :].split(" -> ", 1)[0]
        listed = sorted(versions[identity], key=version_key)
        markers = {version: obsolete_marker(owner, member, obsolete.get(version, [])) for version in listed}
        obsolete_in = [f"{version}:{markers[version]}" for version in listed if markers[version]]
        supported = [version for version in listed if not markers[version]]
        status, reason = classify_forms(owner, member)
        if not supported:
            status, reason = "excluded", "obsolete in every listing version: " + ", ".join(obsolete_in)
        rows.append({
            "surface": "System.Windows.Forms",
            "source_line": source_line,
            "kind": kind,
            "owner": owner,
            "member": member,
            "api": api,
            "family": form_family(owner),
            "disposition": "excluded" if status == "excluded" else "admitted",
            "status": status,
            "reason": reason,
            "source_marker": marker,
            "versions": " ".join(listed),
            "obsolete": " ".join(obsolete_in),
        })
    return rows


def read_drawing_rows(path: Path) -> list[dict[str, str]]:
    """System.Drawing rows carried from an earlier catalogue unchanged."""
    rows = []
    with path.open(encoding="utf-8") as stream:
        for row in csv.DictReader(stream, dialect="excel-tab"):
            if row["surface"] != "System.Drawing":
                continue
            row.setdefault("versions", "")
            row.setdefault("obsolete", "")
            rows.append(row)
    return rows


def read_drawing(paths: list[Path]) -> list[dict[str, str]]:
    rows = []
    for path in paths:
        root = ET.parse(path).getroot()
        for number, node in enumerate(root.findall(".//member"), 1):
            api = node.attrib.get("name", "")
            if not api or len(api) < 3 or api[1] != ":":
                continue
            code, body = api[0], api[2:]
            kind = {"T": "type", "M": "method", "P": "property", "F": "field", "E": "event"}.get(code, "member")
            if kind == "type":
                owner, member = body, ""
            else:
                head = body.split("(", 1)[0]
                owner, _, member = head.rpartition(".")
            status, reason = classify_drawing(owner, member)
            rows.append({
                "surface": "System.Drawing",
                "source_line": f"{path.name}:{number}",
                "kind": kind,
                "owner": owner,
                "member": member,
                "api": api,
                "family": drawing_family(owner),
                "disposition": "excluded" if status == "excluded" else "admitted",
                "status": status,
                "reason": reason,
                "source_marker": "",
                "versions": "",
                "obsolete": "",
            })
    return rows


def write_summary(path: Path, rows: list[dict[str, str]]) -> None:
    surfaces = Counter(row["surface"] for row in rows)
    dispositions = Counter((row["surface"], row["disposition"]) for row in rows)
    statuses = Counter((row["surface"], row["status"]) for row in rows)
    families = Counter((row["surface"], row["family"]) for row in rows)
    lines = [
        "# WinForms and drawing API oversight catalogue",
        "",
        "Status: **OBSERVED upstream inventory; conservative implementation ledger**.",
        "",
        "Forms rows come from the official shipped API lists of `dotnet/winforms` (`PublicAPI.Shipped.txt` of `System.Windows.Forms` and `System.Windows.Forms.Primitives`), one row per public API line, with the versions that list it. `[Obsolete]` markers come from the `Microsoft.WindowsDesktop.App.Ref` reference assemblies of the same versions (`tools/winforms_catalogue/read_obsolete.py`). Sources:",
        "",
        "| .NET | dotnet/winforms branch | Commit | Reference pack |",
        "|---|---|---|---|",
        *[f"| {version} | `{branch}` | `{commit}` | `{pack}` |" for version, branch, commit, pack in OFFICIAL_SOURCES],
        "",
        "Drawing rows are carried unchanged from the earlier catalogue (the pinned .NET 10.0.3 `System.Drawing` reference XML). The generated TSV is the member-level authority; this summary is navigation.",
        "",
        "**Scope rule (owner, 2026-10-13):** a Forms row is in scope when some version of .NET 8.0 or later lists it without `[Obsolete]`, and it is not otherwise excluded below. Rows obsolete in every version that lists them are excluded with their diagnostic.",
        "",
        "A `partial_native` row means only that a corresponding retained GUI.Forms/GUI.Drawing type exists. `measured_partial` identifies an exact behavioral center with named evidence while retaining its stated gaps. Neither status claims that every overload, exception, event order, accessibility projection, or raster result is complete. `missing` is the default for admitted calls without direct evidence.",
        "",
        "## Totals",
        "",
        "| Surface | Rows | Admitted | Excluded |",
        "|---|---:|---:|---:|",
    ]
    for surface in sorted(surfaces):
        lines.append(f"| `{surface}` | {surfaces[surface]} | {dispositions[surface, 'admitted']} | {dispositions[surface, 'excluded']} |")
    forms_missing = statuses["System.Windows.Forms", "missing"]
    lines += [
        "",
        f"The previous catalogue, generated from the LibreWinForms fork's API list, had {PREVIOUS_FORMS_ROWS} Forms rows, {PREVIOUS_FORMS_MISSING} of them `missing`. Against the official surface: {surfaces['System.Windows.Forms']} rows, {forms_missing} `missing`.",
    ]
    since = Counter((row["versions"].split(" ")[0] if row["versions"] else "") for row in rows if row["surface"] == "System.Windows.Forms")
    obsolete_later = sum(1 for row in rows if row["surface"] == "System.Windows.Forms" and row["obsolete"] and row["disposition"] == "admitted")
    lines += ["", "## Forms rows by first listing version", "", "| First listed in | Rows |", "|---|---:|"]
    for version in sorted(since, key=version_key):
        lines.append(f"| .NET {version} | {since[version]} |")
    lines += [
        "",
        f"{obsolete_later} admitted rows are marked `[Obsolete]` in a later version but listed without it in an earlier one; the `obsolete` column names them.",
    ]
    missing_types = sorted(
        row["owner"] for row in rows
        if row["surface"] == "System.Windows.Forms" and row["kind"] == "type" and row["status"] == "missing"
        and row["owner"].count(".") == 3
    )
    missing_members = Counter(
        row["owner"] for row in rows
        if row["surface"] == "System.Windows.Forms" and row["kind"] == "member" and row["status"] == "missing"
    )
    lines += [
        "",
        "## Admitted Forms types still `missing`",
        "",
        f"{len(missing_types)} top-level `System.Windows.Forms` types are admitted and `missing` (nested types are counted with their owner). Controls, components, event arguments, delegates and enumerations are listed together; the number is the type's own `missing` member rows. `missing` means no row is mapped to GUI.Forms evidence yet: some of these have GUI.Forms equivalents that are not mapped (the common dialogs are host services, `Keys` is GUI.Forms' key code, `PropertyGrid` exists without a managed facade), and the rest have no counterpart.",
        "",
        "| Type | Missing members |",
        "|---|---:|",
    ]
    for owner in missing_types:
        lines.append(f"| `{owner.rsplit('.', 1)[-1]}` | {missing_members[owner]} |")
    lines += ["", "## Honest current states", "", "| Surface | State | Rows |", "|---|---|---:|"]
    for (surface, status), count in sorted(statuses.items()):
        lines.append(f"| `{surface}` | `{status}` | {count} |")
    lines += ["", "## Families", "", "| Surface | Family | Rows |", "|---|---|---:|"]
    for (surface, family), count in sorted(families.items()):
        lines.append(f"| `{surface}` | `{family}` | {count} |")
    lines += [
        "",
        "## Policy boundary",
        "",
        "The base excludes ActiveX/browser hosting, printing, MDI, design-time services, classic `DataGrid`, obsolete menu/toolbar/status families, metafile playback, and arbitrary desktop capture. Raw HWND/HDC members are platform-extension work behind opaque leases, never portable public types. PNG is the renderer-core codec; other codec/metadata families remain package review.",
        "",
        "Everything else is an admitted oversight row, not an implementation promise. Promotion to `native` requires a named test or runtime trace for the exact behavioral center; generated identity resolution alone is facade evidence.",
        "",
        "## Regeneration",
        "",
        "See `tools/winforms_catalogue/README.md`. Regeneration is deterministic for the pinned inputs and writes `planning/generated/WINFORMS_API_CATALOGUE.tsv` plus this summary, both kept on the `gui_forms` branch of `malkuth-software/notes`.",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--forms-api", required=True, action="append",
                        help="VERSION=PATH of a PublicAPI.Shipped.txt; repeat per file")
    parser.add_argument("--obsolete", type=Path, help="read_obsolete.py output")
    parser.add_argument("--drawing-xml", type=Path, action="append")
    parser.add_argument("--drawing-rows", type=Path, help="earlier catalogue TSV to carry Drawing rows from")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    args = parser.parse_args()
    if args.drawing_rows is not None:
        drawing = read_drawing_rows(args.drawing_rows)
    elif args.drawing_xml:
        drawing = read_drawing(args.drawing_xml)
    else:
        parser.error("give --drawing-xml or --drawing-rows")
    rows = read_forms(args.forms_api, read_obsolete(args.obsolete)) + drawing
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "surface",
        "source_line",
        "kind",
        "owner",
        "member",
        "api",
        "family",
        "disposition",
        "status",
        "source_marker",
        "reason",
        "versions",
        "obsolete",
    ]
    with args.output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=fields,
            dialect="excel-tab",
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(rows)
    write_summary(args.summary, rows)
    print(json.dumps({"rows": len(rows), "output": str(args.output), "summary": str(args.summary)}, sort_keys=True))


if __name__ == "__main__":
    main()
