#pragma once

#include "gui_forms/commands.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/backstage_view/backstage_view.hpp"
#include "gui_forms/controls/button_base/check_box/check_box.hpp"
#include "gui_forms/controls/panel/combo_box/combo_box.hpp"
#include "gui_forms/controls/panel/numeric_up_down/numeric_up_down.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/ribbon_color_picker/ribbon_color_picker.hpp"
#include "gui_forms/controls/ribbon_font_control/ribbon_font_control.hpp"
#include "gui_forms/controls/ribbon_gallery/ribbon_gallery.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/window/native_popup/popup_lift.hpp"
#include "gui_forms/controls/ribbon/ribbon_capabilities/ribbon_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

class RibbonGroupOverlay;
class RibbonApplicationMenu;

// The kinds of ribbon command (stage RB, owner decision 2: commands are
// records in a command item system, hosted editors are child controls).
// - button runs its Command;
// - toggle flips its Command's checked state, then runs it;
// - split runs its Command from its face and opens its menu from its arrow;
// - drop_down opens its menu;
// - combo_box, numeric_up_down, check_box and text_box hold a real
//   ComboBox, NumericUpDown, CheckBox or TextBox child control;
// - gallery holds the application's in-ribbon RibbonGallery, which gives up
//   one column at a time as the ribbon narrows (decision 8);
// - font_control and color_picker hold the application's RibbonFontControl
//   or RibbonColorPicker, each bound to its value command (phase 6);
// - label is plain text in a row, such as a hint beside the commands it
//   explains (Plan Paint's placement and picker hints; the label item of
//   ToolStrip and the study's command model): it takes no Command, image
//   or menu, is never large, takes no pointer or keyboard focus and is
//   published as static text. Its `text` is translatable like any
//   command's.
enum class RibbonCommandKind : std::uint8_t {
    button,
    toggle,
    split,
    drop_down,
    combo_box,
    numeric_up_down,
    check_box,
    text_box,
    gallery,
    font_control,
    color_picker,
    label,
};

// A button-family command's image size: large (a 32-unit icon over its
// label, the group's full height) or small (a 16-unit icon beside its label,
// stacked in rows). `RibbonCommandSpec::size` is the size at the group's
// largest layout; the group's layouts may make it small (phase 2b).
enum class RibbonCommandSize : std::uint8_t { large, small };

// One command record. `stable_id` is unique within the ribbon (across tabs,
// groups and commands) and names the command in events, semantics and the
// published anatomy.
//
// A button, toggle or split takes its text (when `text` is empty), enabled,
// visible and checked state from its required `command`, and runs it on a
// click; a drop-down's command is optional. `text` may carry a mnemonic
// ("&Paste"). Labels are always shown (owner decision 6); a small command
// with an image may leave its label out (`show_label` false: an authored
// icon-only command, decision 30). `drop_down` is the menu of a split or
// drop-down command. A hosted command's `text` is its control's accessible
// name (and a check box's caption); `choices` are a combo box's items and
// `width` its field's width in logical units (zero for the default). A text
// box or numeric up-down also shows its `text` as a label part (Office's
// field label): at the field's left when its group stacks rows, above the
// field when the group's layout has one row; `show_label` false leaves an
// unlabelled field. A gallery
// command's `gallery` is an in-ribbon RibbonGallery the application made and
// configured; the ribbon adds it as its child and sets its column count. A
// font control's `font_control` and a colour picker's `color_picker` are,
// likewise, the application's controls, which the ribbon adds and places on
// one row.
//
// `beside_previous` places a small or hosted command in the previous
// command's row, to its right, instead of in a row of its own (the Windows
// Ribbon Framework's control groups, which stay together in a row): Save
// over Undo and Redo is Save, then Undo, then Redo beside Undo. A row of
// commands side by side counts as one of the layout's rows, and the column
// is as wide as its widest row. It is ignored where the command, or the
// one before it, is large or a gallery in the layout in force; the
// group's first command may not set it.
struct RibbonCommandSpec final {
    std::string stable_id{};
    RibbonCommandKind kind{RibbonCommandKind::button};
    RibbonCommandSize size{RibbonCommandSize::large};
    std::shared_ptr<Command> command{};
    GUI_FORMS_LOCALIZABLE(text) std::string text{};
    ImageId image{};
    bool show_label{true};
    std::vector<MenuItemSpec> drop_down{};
    std::vector<std::string> choices{};
    double width{};
    std::shared_ptr<RibbonGallery> gallery{};
    std::shared_ptr<RibbonFontControl> font_control{};
    std::shared_ptr<RibbonColorPicker> color_picker{};
    bool beside_previous{};
};

// A group's size in one of its layouts, named after the Windows Ribbon
// Framework's group sizes, largest first. A collapsed group shows one button
// (its icon, caption and a chevron, owner decision 27) that opens the group
// at its largest layout in an overlay.
enum class RibbonGroupSize : std::uint8_t { large, medium, small, collapsed };

// One layout of a group (the WPF/MFC model, owner decision 5): the image size
// of each command and how many small commands stack in a column. Labels
// never drop (decision 6), so a layout only changes image sizes and rows.
// `command_sizes` holds one size per command of the group, in command order,
// or is empty for the commands' own sizes (`RibbonCommandSpec::size`);
// entries for hosted and gallery commands are ignored. A collapsed layout
// has no sizes. `rows` (1 to 3) is the number of small and hosted command
// rows per column. The rows stand from the top of the plane at the
// three-row pitch, so a column of one or two rows aligns with the tops of
// the large commands beside it rather than centring in the plane.
struct RibbonGroupLayout final {
    RibbonGroupSize size{RibbonGroupSize::large};
    std::vector<RibbonCommandSize> command_sizes{};
    std::size_t rows{3U};
};

// The application modes a tab or group belongs to (stage RB, phase 5; owner
// decision 14, F2-1, after the Windows Ribbon Framework's application modes):
// one bit per mode, 32 modes. A tab or group shows while it shares a bit with
// Ribbon::application_modes. Every mode by default.
inline constexpr std::uint32_t ribbon_all_modes = 0xFFFFFFFFU;

// A group of commands on a tab, with a caption beneath and, when
// `dialog_launcher` is set, a launcher button in the caption's corner.
// `image` is the icon of the group's collapsed button (none: the first
// command image). `layouts` are its size definitions, largest first; empty
// layouts are generated (Ribbon::group_layouts). Authored layouts that do
// not end in the collapsed button gain it as their last step, so that a
// group never vanishes from a narrow plane (a group of one command never
// collapses). `modes` are the application modes it shows in.
struct RibbonGroupSpec final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(text) std::string caption{};
    bool dialog_launcher{};
    std::vector<RibbonCommandSpec> commands{};
    ImageId image{};
    std::vector<RibbonGroupLayout> layouts{};
    std::uint32_t modes{ribbon_all_modes};
};

// One authored step of a tab's reduction order (owner decisions 5, 29):
// `group` (a group's stable ID on the same tab) moves to its next layout, or
// its gallery gives up one column (decision 8).
enum class RibbonReductionKind : std::uint8_t { layout, gallery_column };
struct RibbonReductionStep final {
    std::string group{};
    RibbonReductionKind kind{RibbonReductionKind::layout};
};

// A tab. `text` may carry a mnemonic ("&Home"). `reduction_order` is the
// order in which its groups shrink as the ribbon narrows; after it (or
// without it) every group that can still shrink does so one step at a time,
// right to left, repeating (WPF's cycle, decision 32). `tab_set` names the
// contextual tab set the tab belongs to (empty: a core tab); `modes` are the
// application modes it shows in. `width` is an authored width in logical
// units, scaled with the text as the tab row is (none: the label's width,
// at its widest state font, plus the theme's padding; Plan Paint's tabs
// carry their own widths); above zero and at most Ribbon::maximum_tab_width.
// A tab that is not `enabled` shows in the disabled state and cannot be
// selected: the pointer, the keyboard, mnemonics and accessibility pass it
// by, and a selection that would fall on it falls back to another tab.
//
// A panel tab: `panel` is a control the application made, which the ribbon
// adds as its child and sizes to the whole command plane while the tab is
// selected, in place of groups (the application's own command area, such
// as a palette or an atlas). It has no groups and no reduction order, and is
// unowned or already this ribbon's; the ribbon removes it when a new
// definition no longer names it. Its keyboard stops and semantics are its
// own, as a hosted control's are.
struct RibbonTabSpec final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(text) std::string text{};
    std::vector<RibbonGroupSpec> groups{};
    std::vector<RibbonReductionStep> reduction_order{};
    std::string tab_set{};
    std::uint32_t modes{ribbon_all_modes};
    std::shared_ptr<Control> panel{};
    std::optional<double> width{};
    bool enabled{true};
};

// A contextual tab set (stage RB, phase 5): tabs that show while the
// application makes the set available, such as while a picture is selected.
// A shown set is marked by a tint from the theme's accent ramp on its tabs
// and on the command plane of its selected tab (owner decisions 14, 22, 25),
// with no captioned band; `caption` ("Picture Tools") names the set to
// accessibility.
struct RibbonTabSetSpec final {
    std::string stable_id{};
    std::string caption{};
};

// A tab set's availability, as the application sets it (the Windows Ribbon
// Framework's three states): not shown; shown; shown and its tab selected.
enum class RibbonTabSetAvailability : std::uint8_t { not_available, available, active };

// One group's place in a ladder entry: its layout (an index into
// Ribbon::group_layouts), that layout's size, and its gallery's column count
// (zero without a gallery).
struct RibbonLadderGroupState final {
    std::size_t layout{};
    RibbonGroupSize size{RibbonGroupSize::large};
    std::size_t gallery_columns{};

    friend bool operator==(const RibbonLadderGroupState& left, const RibbonLadderGroupState& right) noexcept {
        return left.layout == right.layout && left.size == right.size &&
               left.gallery_columns == right.gallery_columns;
    }
};

// One entry of a tab's reduction ladder: every group's state and the width,
// in logical units, the groups need side by side. Entries run from the widest
// to the narrowest; widths strictly decrease.
struct RibbonLadderEntry final {
    std::vector<RibbonLadderGroupState> groups{};
    double width{};
};

// Structured scaling instrumentation (RIBBON_SCALING_STUDY.md §5.7).
// `ladder_builds` counts ladders built, `ladder_entries` and
// `nonshrinking_steps` describe the last one (a step that did not narrow the
// tab is folded into the next entry), `ladder_lookups` counts layouts that
// chose an entry, and `text_measurements` counts the ribbon's own text
// measurements (labels, captions and tabs, each measured once per cache).
struct RibbonScalingCounters final {
    std::uint64_t ladder_builds{};
    std::uint64_t ladder_entries{};
    std::uint64_t nonshrinking_steps{};
    std::uint64_t ladder_lookups{};
    std::uint64_t text_measurements{};
};

// A click on a button-family command (or its split face), after its Command
// ran.
struct RibbonCommandClick final {
    std::string ribbon_id{};
    std::string command_id{};
};

// A choice from a split or drop-down command's menu.
struct RibbonDropDownInvocation final {
    std::string ribbon_id{};
    std::string command_id{};
    MenuItemInvocation choice{};
};

// One item of the application menu's recent pane (stage RB, phase 6): a
// recently used document, shown numbered under the pane's caption, with its
// `description` (a folder, say) for accessibility.
struct RibbonRecentItem final {
    std::string stable_id{};
    std::string text{};
    std::string description{};
};

// A choice from the recent pane.
struct RibbonRecentItemInvocation final {
    std::string ribbon_id{};
    std::string item_id{};
};

// A click on a group's dialog launcher.
struct RibbonDialogLaunch final {
    std::string ribbon_id{};
    std::string group_id{};
};

// The selected tab after it changed.
struct RibbonTabChange final {
    std::string ribbon_id{};
    std::size_t tab{};
    std::string tab_id{};
};

// A ribbon (stage RB, phases 1 and 2b): the File tab and the core tabs on a
// tab row that is always visible, and the command plane of the selected tab
// with its groups, captions, dividers, dialog launchers and commands.
// Clicking the selected tab minimizes the command plane or shows it again
// (owner decision 19); clicking another tab selects it and shows the plane.
// The File tab opens the application menu over the ribbon (decisions 3, 23).
// Commands are records bound to shared Commands, laid out as the "command"
// item system; hosted editors and galleries are child controls placed by the
// ribbon's layout. Every part is painted through the by-role paint path.
//
// Scaling (phase 2b): each tab has a reduction ladder, built from its
// groups' layouts and its reduction order and measured once per content,
// font, text scale and theme; the plane shows the widest entry that fits,
// and scrolls by whole groups when even the narrowest does not (decision
// 35). The plane, never the tab row, hides when the document area under the
// ribbon would fall below the minimum it declares (decisions 28, 34, 36).
//
// Contextual tab sets and application modes (phase 5): sets show and hide by
// the application's availability calls, tinted from the theme's accent ramp,
// with the built-in selection rules; modes choose the tabs and groups shown.
//
// The file area (phase 6): the File tab opens Backstage when the ribbon has
// one, else the application menu, in two panes while there are recent items.
//
// Text and number fields show their text as a label part, at the field's
// left or above it in a one-row layout; a panel tab shows the application's
// control as its command plane; the ribbon and its overlays follow the
// window's theme and colour scheme as they change.
//
// Not yet: hosting the QuickAccessToolbar on the tab row (it is a separate
// control the application places), the minimized ribbon's temporary overlay,
// right-to-left mirroring and two-line large labels.
class Ribbon final : public Control, public RibbonCapabilities<Ribbon> {
public:
    // Development bounds, not measured capacity.
    static constexpr std::size_t maximum_tabs = 32U;
    static constexpr std::size_t maximum_groups_per_tab = 32U;
    static constexpr std::size_t maximum_commands_per_group = 64U;
    static constexpr std::size_t maximum_layouts_per_group = 8U;
    static constexpr std::size_t maximum_reduction_steps = 1024U;
    // How far, in logical units, the document area must clear its minimum
    // before a hidden command plane shows again (owner decision 36).
    static constexpr double hide_hysteresis = 8.0;
    // How far, in logical units, the plane must clear the narrowest ladder
    // entry before scrolling ends (owner decision 45).
    static constexpr double scroll_hysteresis = 8.0;
    // The widest authored tab width (RibbonTabSpec::width), in logical units.
    static constexpr double maximum_tab_width = 1024.0;

    explicit Ribbon(StableId stable_id);
    ~Ribbon() override;

    [[nodiscard]] std::span<const RibbonTabSpec> tabs() const noexcept { return tabs_; }
    // Replaces the tabs, keeping the selection on the tab with the same ID
    // (or the first). Throws std::invalid_argument, keeping the previous tabs,
    // for an empty or repeated ID (IDs are one namespace across tabs, groups
    // and commands), invalid UTF-8, a button, toggle or split without a
    // command, a menu on a kind that has none or a split without one, an
    // icon-only command that is not small (in any layout) or has no image,
    // choices on a command that is not a combo box, a width outside 0 to
    // 1024, an invalid menu, a gallery command without an in-ribbon gallery
    // (or with one used twice or owned by another parent), two galleries in
    // one group, an invalid layout (a collapsed layout that is not last or
    // in a group of one command, sizes that grow, a size count that is not
    // the command count, rows outside 1 to 3), a reduction step naming a
    // group not on its tab, shrinking a group past its last layout or its
    // gallery past its minimum (owner decision 29), or more than the bounds.
    //
    // Contextual tabs follow the core tabs: `sets` declares the tab sets, and
    // each set's tabs come after every core tab, together, in the sets'
    // order. Throws std::invalid_argument, too, for a tab naming an
    // undeclared set, a set without tabs, tabs out of that order, a set ID
    // that is not unique in the ribbon's ID namespace, or zero modes. Sets
    // keep their availability across a set_tabs by ID.
    void set_tabs(std::vector<RibbonTabSpec> tabs, std::vector<RibbonTabSetSpec> sets = {});
    [[nodiscard]] std::span<const RibbonTabSetSpec> tab_sets() const noexcept { return tab_sets_; }
    // A set's availability. Making a set active selects the tab of the set
    // last chosen by the user, if any, else its first tab; making it
    // available only shows it; making it not available while one of its
    // tabs is selected selects the core tab chosen last, else the first core
    // tab shown (owner decision 14, F3-1). Throws std::out_of_range for an
    // unknown set.
    [[nodiscard]] RibbonTabSetAvailability tab_set_availability(std::string_view set_id) const;
    void set_tab_set_availability(std::string_view set_id, RibbonTabSetAvailability availability);
    // The accent a shown set is tinted with, or none while it is hidden.
    // A newly shown set takes an index no other shown set has, keeping its
    // previous one when it can. Throws std::out_of_range for an unknown set.
    [[nodiscard]] std::optional<AccentIndex> tab_set_accent(std::string_view set_id) const;
    // Whether a tab shows on the tab row now (its modes, and its set's
    // availability). Throws std::out_of_range for a tab past the end.
    [[nodiscard]] bool tab_shown(std::size_t tab) const;
    // A group's layouts, largest first: as authored, or generated when none
    // were (each step makes the rightmost run of up to three large commands
    // small, then the group collapses unless it holds one command). Throws
    // std::out_of_range for a tab or group past the end.
    [[nodiscard]] std::span<const RibbonGroupLayout> group_layouts(std::size_t tab, std::size_t group) const;
    // A tab's reduction ladder at the current font, text scale and theme,
    // built if needed. Throws std::out_of_range for a tab past the end.
    [[nodiscard]] std::vector<RibbonLadderEntry> reduction_ladder(std::size_t tab) const;
    // The ladder entry the command plane shows, or none while it is hidden,
    // minimized or there is no tab.
    [[nodiscard]] std::optional<std::size_t> ladder_step() const;
    // Whether the plane scrolls (owner decision 35): it starts when even the
    // narrowest entry is wider than the plane and ends only once the plane is
    // scroll_hysteresis wider than that entry (decision 45). The first group
    // shown while it scrolls.
    [[nodiscard]] bool scrolling() const;
    [[nodiscard]] std::size_t first_visible_group() const noexcept { return first_group_; }
    // Whether the document-area rule hides the command plane now.
    [[nodiscard]] bool plane_hidden() const noexcept { return plane_hidden_; }
    [[nodiscard]] RibbonScalingCounters scaling_counters() const noexcept { return counters_; }

    // The File tab's menu (its text is application_text).
    [[nodiscard]] const std::vector<MenuItemSpec>& application_menu() const noexcept { return application_menu_; }
    // Throws std::invalid_argument for an invalid menu.
    void set_application_menu(std::vector<MenuItemSpec> items);
    // Opens the File tab's file area: Backstage when the ribbon has one;
    // otherwise the application menu below the File tab, in two panes while
    // there are recent items. False without items (or Backstage) or a window.
    bool show_application_menu(MenuOpenMode mode = MenuOpenMode::keyboard);
    // The application menu's recent pane (phase 6): its items and caption
    // ("Recent documents"). While there are recent items the application
    // menu takes only command and separator items. Throws
    // std::invalid_argument for an empty or repeated ID, invalid or
    // multi-line UTF-8, more than maximum_recent_items, or recent items
    // beside an application menu with other kinds (and set_application_menu
    // likewise for such items beside recent ones).
    static constexpr std::size_t maximum_recent_items = 50U;
    [[nodiscard]] std::span<const RibbonRecentItem> recent_items() const noexcept { return recent_items_; }
    void set_recent_items(std::vector<RibbonRecentItem> items);
    // The two-pane menu's content control, null before its first opening.
    [[nodiscard]] Control::Ptr application_menu_overlay() const noexcept;
    bool show_backstage();
    void close_backstage() noexcept;
    [[nodiscard]] bool backstage_open() const noexcept { return backstage_open_; }
    // Opens a split or drop-down command's menu; false for another kind, a
    // disabled command or one not on the selected tab.
    bool show_drop_down(std::string_view command_id, MenuOpenMode mode = MenuOpenMode::keyboard);
    void close_menus() noexcept;
    [[nodiscard]] bool menu_open() const noexcept;
    // Opens a collapsed group of the selected tab in an overlay below the ribbon,
    // at its largest layout (owner decision 27); the group's hosted controls
    // move into the overlay while it is open. False for a group that is not
    // collapsed, not on the selected tab, or without a window. With
    // MenuOpenMode::keyboard focus goes to the overlay's first command.
    bool show_group_overlay(std::string_view group_id, MenuOpenMode mode = MenuOpenMode::keyboard);
    // Closes the group overlay; focus returns to the group's button.
    void close_group_overlay() noexcept;
    // The open overlay's group ID, or none.
    [[nodiscard]] std::optional<std::string> overlay_group() const;
    // The overlay's content control (its parts are in its local coordinates),
    // null before the first overlay.
    [[nodiscard]] Control::Ptr group_overlay() const noexcept;

    // The hosted control of a command. Throws std::out_of_range for an ID
    // that names no command of that kind.
    [[nodiscard]] std::shared_ptr<ComboBox> combo_box(std::string_view command_id) const;
    [[nodiscard]] std::shared_ptr<NumericUpDown> numeric_up_down(std::string_view command_id) const;
    [[nodiscard]] std::shared_ptr<CheckBox> check_box(std::string_view command_id) const;
    [[nodiscard]] std::shared_ptr<TextBox> text_box(std::string_view command_id) const;

    [[nodiscard]] Event<const RibbonCommandClick&>& command_clicked() noexcept { return command_clicked_; }
    [[nodiscard]] Event<const RibbonDropDownInvocation&>& drop_down_item_invoked() noexcept {
        return drop_down_item_invoked_;
    }
    [[nodiscard]] Event<const RibbonDialogLaunch&>& dialog_launcher_clicked() noexcept {
        return dialog_launcher_clicked_;
    }
    [[nodiscard]] Event<const RibbonTabChange&>& selected_tab_changed() noexcept { return selected_tab_changed_; }
    [[nodiscard]] Event<bool>& minimized_changed() noexcept { return minimized_changed_; }
    [[nodiscard]] Event<const MenuItemInvocation&>& application_menu_item_invoked() noexcept {
        return application_menu_item_invoked_;
    }
    [[nodiscard]] Event<const RibbonRecentItemInvocation&>& recent_item_invoked() noexcept {
        return recent_item_invoked_;
    }
    [[nodiscard]] Event<bool>& backstage_open_changed() noexcept { return backstage_open_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_pointer_preview(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    // Semantics and accessibility only: on_focus_changed repaints the key
    // stop alone (repaint_stop), not the whole ribbon.
    [[nodiscard]] Dirty focus_invalidation() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members; its states are Control's.
    struct Parts final {
        PartGeometry region;
        PartGeometry tab_rail;
        PartGeometry application_button;
        PartGeometry application_label;
        PartGeometry command_plane;
        PartGeometry scroll_left;
        PartGeometry scroll_left_glyph;
        PartGeometry scroll_right;
        PartGeometry scroll_right_glyph;
        // Reserved for the help button (owner, N4); never present yet.
        PartGeometry help_button;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The "tab" item system: one core tab on the tab row.
    struct TabParts final {
        PartGeometry tab;
        // The hover layer over the tab's face: the engine's dithered rise
        // (flat at rest), drawn within the tab's body.
        PartGeometry tab_hover;
        PartGeometry tab_label;
        PartGeometry tab_focus;
    };
    struct TabStates final {
        bool hot{};
        bool selected{};
        bool focus_visible{};
        bool disabled{};
    };
    struct Tab final {
        std::size_t index{};
        std::string stable_id;
        TabParts parts;
        TabStates states;
    };
    [[nodiscard]] std::vector<Tab> realized_tabs() const;

    // The "group" item system: one group of the selected tab. A collapsed
    // group shows its button parts and no caption or launcher.
    struct GroupParts final {
        PartGeometry group_region;
        PartGeometry group_caption;
        PartGeometry group_divider;
        PartGeometry group_launcher;
        PartGeometry group_launcher_glyph;
        PartGeometry group_launcher_focus;
        PartGeometry group_button;
        PartGeometry group_button_icon;
        PartGeometry group_button_label;
        PartGeometry group_button_glyph;
        PartGeometry group_button_focus;
    };
    struct Group final {
        std::size_t index{};
        std::string stable_id;
        std::size_t layout{};
        bool collapsed{};
        GroupParts parts;
    };
    [[nodiscard]] std::vector<Group> realized_groups() const;

    // The "command" item system: one command of the selected tab.
    struct CommandParts final {
        PartGeometry command_face;
        PartGeometry command_icon;
        PartGeometry command_label;
        PartGeometry command_arrow;
        PartGeometry command_arrow_glyph;
        PartGeometry command_focus;
        PartGeometry command_combo_box;
        PartGeometry command_numeric_up_down;
        PartGeometry command_check_box;
        PartGeometry command_text_box;
        PartGeometry command_gallery;
        PartGeometry command_font_control;
        PartGeometry command_color_picker;
    };
    struct CommandStates final {
        bool hot{};
        bool pressed{};
        bool checked{};
        bool open{};
        bool disabled{};
        bool focus_visible{};
    };
    struct CommandItem final {
        std::size_t group{};
        std::size_t index{};
        std::string stable_id;
        CommandParts parts;
        CommandStates states;
    };
    [[nodiscard]] std::vector<CommandItem> realized_commands() const;
    // The open group overlay's group and commands, in the overlay's local
    // coordinates; empty while no overlay is open.
    [[nodiscard]] std::vector<Group> realized_overlay_groups() const;
    [[nodiscard]] std::vector<CommandItem> realized_overlay_commands() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attachment_committed() noexcept override;
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void on_dispose() noexcept override;
    [[nodiscard]] bool mnemonic_matches(char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    friend class RibbonGroupOverlay;
    friend class RibbonApplicationMenu;

    // A command's position in the selected tab: its group and its place.
    struct CommandAddress final {
        std::size_t group{};
        std::size_t command{};

        friend constexpr bool operator==(CommandAddress left, CommandAddress right) noexcept {
            return left.group == right.group && left.command == right.command;
        }
    };
    // Which part of a command a point or a press is on.
    enum class CommandZone : std::uint8_t { none, main, arrow };
    // A keyboard stop: the File tab, a tab, a command of the selected tab, a
    // group's dialog launcher, or a collapsed group's button.
    enum class StopKind : std::uint8_t { application, tab, command, launcher, group_button };
    struct Stop final {
        StopKind kind{StopKind::tab};
        std::size_t index{};
        CommandAddress command{};

        friend constexpr bool operator==(const Stop& left, const Stop& right) noexcept {
            return left.kind == right.kind && left.index == right.index && left.command == right.command;
        }
    };
    // What a point is on.
    struct Hit final {
        enum class Kind : std::uint8_t {
            none,
            application,
            tab,
            command,
            launcher,
            group_button,
            scroll_left,
            scroll_right,
        } kind{Kind::none};
        std::size_t index{};
        CommandAddress command{};
        CommandZone zone{CommandZone::none};
    };
    // Where everything sits, from one layout pass: the ribbon's own view
    // (the tab row and the selected tab's plane at one ladder entry), or the
    // group overlay's view (`overlay`: only the overlay's group, at its largest
    // layout, in the overlay's local coordinates; no tab row).
    struct Layout final {
        Rect tab_rail{};
        Rect application{};
        std::vector<Rect> tabs;
        Rect plane{};
        // The part of the plane between its scroll buttons, where groups show.
        Rect groups_area{};
        Rect scroll_left{};
        Rect scroll_right{};
        std::optional<std::size_t> step{};
        bool scrolling{};
        bool overlay{};
        // Per group of the selected tab: bounds (empty when not shown), the
        // layout index and size, and gallery columns.
        std::vector<Rect> groups;
        std::vector<std::size_t> group_layouts;
        std::vector<RibbonGroupSize> group_sizes;
        std::vector<std::size_t> gallery_columns;
        std::vector<std::vector<Rect>> commands;  // per group, per command
    };
    // A command's hosted control (or gallery) and the command watch
    // subscription.
    struct CommandRecord final {
        Control::Ptr hosted{};
        SubscriptionToken watch{};
        // The command's last text and visibility, to tell a change that
        // needs layout from one that needs only a repaint.
        std::string text{};
        bool visible{true};
    };
    // A named callback: a Command's state changed. `tab`, `group` and
    // `command` locate the record.
    struct CommandWatch final {
        Ribbon* ribbon{};
        std::size_t tab{};
        std::size_t group{};
        std::size_t command{};
        void operator()(const CommandState&) const;
    };

    // -- scaling caches (RIBBON_SCALING_STUDY.md §5.7) ------------------------
    // What every measured width depends on: the content (tabs, command text
    // and visibility), the font, the text scale, the theme and the text
    // metrics provider. A changed key empties every cache below.
    struct ScalingKey final {
        std::uint64_t content{};
        double text_scale{};
        const Theme* theme{};
        const TextMetricsProvider* provider{};
        FontSpec font{};
        // What the theme's rules are matched against beside the theme: the
        // window's media facts (its colour scheme among them), its user
        // theme's rules and the ribbon's inline style; the tab row's
        // metrics come from them (RowMetrics).
        MediaFacts media{};
        const CompiledThemeRules* user_rules{};
        const CompiledThemeRules* inline_rules{};

        friend bool operator==(const ScalingKey& left, const ScalingKey& right) noexcept {
            return left.content == right.content && left.text_scale == right.text_scale &&
                   left.theme == right.theme && left.provider == right.provider && left.font == right.font &&
                   left.media == right.media && left.user_rules == right.user_rules &&
                   left.inline_rules == right.inline_rules;
        }
    };
    // One command's widths at each image size, measured once. A hosted
    // command's row width is its small width; a gallery's width depends on
    // its columns and is read from the gallery.
    // A command's widths: as a large command (or a labelled field with its
    // label above) and as a small command (or a field with its label at its
    // left).
    struct CommandMetrics final {
        double large_width{};
        double small_width{};
    };
    // A labelled field's command bounds split into its label and its field;
    // `label` is empty for an unlabelled field.
    struct FieldSplit final {
        Rect label{};
        Rect field{};
    };
    // A gallery's column range and widest width as measured, so that a
    // change made on the gallery itself rebuilds the ladder.
    struct GalleryMetrics final {
        std::size_t minimum{};
        std::size_t maximum{};
        double maximum_width{};

        friend bool operator==(const GalleryMetrics& left, const GalleryMetrics& right) noexcept {
            return left.minimum == right.minimum && left.maximum == right.maximum &&
                   left.maximum_width == right.maximum_width;
        }
    };
    // One tab's measured content and its ladder. Per group: commands'
    // metrics, the caption row's width (caption and launcher), the collapsed
    // button's inner width, and the gallery command and its metrics.
    struct TabScaling final {
        bool built{};
        std::vector<std::vector<CommandMetrics>> commands;
        std::vector<double> captions;
        std::vector<double> collapsed;
        std::vector<std::optional<std::size_t>> galleries;
        std::vector<GalleryMetrics> gallery_metrics;
        std::vector<RibbonLadderEntry> ladder;
    };
    // The tab row's measured widths and the theme's metrics for it (look
    // integration, Plan Paint defect 5), each scaled by the text scale: the
    // rail's height (`height` on the tab rail), the plane's (`height` on the
    // command plane), the tabs' padding and gap, and how far a tab's top
    // sits below the rail's top and its bottom above the rail's bottom, at
    // rest and selected (`margin-top`, `margin-bottom`; a negative bottom
    // margin runs the tab into the plane). Each falls back to the ribbon's
    // own metric where no rule sets it.
    struct TabMargins final {
        double top{};
        double bottom{};
    };
    struct RowMetrics final {
        bool built{};
        double application{};
        std::vector<double> tabs;
        double row_height{};
        double plane_height{};
        double padding{};
        double application_padding{};
        double gap{};
        TabMargins application_margins{};
        TabMargins rest{};
        TabMargins selected{};
    };

    // -- contextual sets and modes (ribbon_contextual.cpp) ---------------------
    // One set's state: its availability, the accent it shows with (and the
    // one it showed with last, kept while hidden), the set tab the user chose
    // last (until a core tab or another set's tab is chosen; RIBBON_STUDY.md
    // §5.4), and its tabs' range in tabs_.
    struct TabSetState final {
        RibbonTabSetAvailability availability{RibbonTabSetAvailability::not_available};
        std::optional<AccentIndex> accent{};
        std::optional<AccentIndex> last_accent{};
        std::optional<std::size_t> remembered_tab{};
        std::size_t first_tab{};
        std::size_t tab_count{};
    };
    // Who changed the selection: the user (or the application on the user's
    // behalf, through selected_tab), whose choices the selection rules remember,
    // or a selection rule, whose choices they do not.
    enum class SelectionCause : std::uint8_t { user, rule };
    [[nodiscard]] bool shown(std::size_t tab) const noexcept;
    // Whether a tab may be selected: shown and enabled.
    [[nodiscard]] bool selectable(std::size_t tab) const noexcept;
    [[nodiscard]] bool group_shown(std::size_t tab, std::size_t group) const noexcept;
    [[nodiscard]] std::optional<std::size_t> find_tab_set(std::string_view set_id) const noexcept;
    [[nodiscard]] std::optional<AccentIndex> tab_accent(std::size_t tab) const noexcept;
    [[nodiscard]] std::optional<std::size_t> fallback_tab() const noexcept;
    // A shown tab's 1-based place among the shown tabs of its kind (the core
    // tabs, or its set's tabs) and how many of them there are.
    struct TabPosition final {
        std::size_t position{};
        std::size_t count{};
    };
    [[nodiscard]] TabPosition position_among_shown(std::size_t tab) const noexcept;
    void refresh_accents();
    // Theme following: the window's theme or colour scheme changed.
    void on_window_theme_changed(const Theme& theme);
    void on_presentation_changed(const PresentationSettings& settings);
    void follow_theme();
    void sync_overlay_themes();
    void sync_backstage_theme();
    void release_backstage_theme() noexcept;
    [[nodiscard]] std::shared_ptr<const Theme> nearest_theme_override() const;
    void tabs_shown_changed();

    // -- set_tabs stages (ribbon.cpp) -------------------------------------------
    // What set_tabs prepares before it changes anything: each tab's set (none
    // for a core tab) and every group's layouts, indexed [tab][group].
    struct PreparedTabs final {
        std::vector<std::optional<std::size_t>> tab_set_of;
        std::vector<std::vector<std::vector<RibbonGroupLayout>>> layouts;
    };
    // The hosted controls taken out of the records set_tabs replaces:
    // ribbon-made controls with their records (and Command watches), keyed
    // "<command ID>/<kind>", and the application's controls (galleries, font
    // controls, colour pickers), matched by identity.
    struct HostedCarryOver final {
        std::vector<std::pair<std::string, CommandRecord>> made;
        std::vector<Control::Ptr> authored;
    };
    [[nodiscard]] PreparedTabs prepare_tabs(const std::vector<RibbonTabSpec>& tabs,
                                            const std::vector<RibbonTabSetSpec>& sets) const;
    [[nodiscard]] HostedCarryOver take_hosted_controls();
    [[nodiscard]] std::vector<TabSetState> carried_set_states(
        const std::vector<RibbonTabSetSpec>& sets, const std::vector<std::optional<std::size_t>>& tab_set_of) const;
    void rebuild_records(HostedCarryOver& carried);
    void release_carried(const HostedCarryOver& carried);

    // A realized item located: a tab, a group or a command of the selected
    // tab, or nothing past the end.
    struct RealizedItem final {
        enum class Kind : std::uint8_t { none, tab, group, command } kind{Kind::none};
        std::size_t index{};
        CommandAddress command{};
    };
    [[nodiscard]] RealizedItem locate_realized(const Layout& placed, std::size_t item) const;
    [[nodiscard]] std::vector<RealizedItem> realized_list(const Layout& placed) const;
    [[nodiscard]] Layout layout() const;
    [[nodiscard]] Layout layout_for(Rect client) const;
    [[nodiscard]] Layout overlay_layout(Rect panel) const;
    void reset_layout_groups(Layout& placed) const;
    void place_plane(Layout& placed) const;
    [[nodiscard]] double place_group(const TabScaling& scaling, std::size_t tab, std::size_t group,
                                     const RibbonGroupLayout& group_layout, std::size_t columns, double x, double top,
                                     std::vector<Rect>& commands) const;
    [[nodiscard]] double tab_row_height() const;
    [[nodiscard]] double plane_height() const;
    [[nodiscard]] ControlPartMetrics part_metrics(std::string_view part, ControlVisualRole family,
                                                  AnatomyStateSet states) const;
    [[nodiscard]] FontSpec tab_label_font(AnatomyStateSet states) const;
    [[nodiscard]] double tab_label_width(std::string_view text, AnatomyStateSet states) const;
    [[nodiscard]] double text_width(std::string_view text) const;
    [[nodiscard]] ScalingKey current_scaling_key() const;
    void refresh_scaling_key() const;
    [[nodiscard]] const RowMetrics& row_metrics() const;
    [[nodiscard]] const TabScaling& tab_scaling(std::size_t tab) const;
    void measure_tab(std::size_t tab, TabScaling& scaling) const;
    void build_ladder(std::size_t tab, TabScaling& scaling) const;
    [[nodiscard]] double state_width(const TabScaling& scaling, std::size_t tab,
                                     const std::vector<RibbonLadderGroupState>& state) const;
    // `box` is a check box command's hosted CheckBox, or null; its label
    // is measured in the CheckBox's own text font, not the ribbon's.
    [[nodiscard]] CommandMetrics measure_command(const RibbonCommandSpec& command, const CheckBox* box) const;
    [[nodiscard]] bool field_labelled(const RibbonCommandSpec& command) const;
    // Whether command `command` of `group` in `group_layout` is a small text
    // label for the field after it: a small button-family command with a
    // label and no image (or a label command), followed by a visible hosted
    // command other than a check box (which carries its own label) or a
    // gallery, not beside it.
    [[nodiscard]] bool labels_next_field(const RibbonGroupSpec& group, const RibbonGroupLayout& group_layout,
                                         std::size_t command) const;
    [[nodiscard]] double hosted_field_width(const RibbonCommandSpec& command) const;
    [[nodiscard]] FieldSplit split_field(const RibbonCommandSpec& command, Rect bounds) const;
    [[nodiscard]] TabParts tab_parts(const Layout& placed, std::size_t tab) const;
    [[nodiscard]] GroupParts group_parts(const Layout& placed, std::size_t group) const;
    [[nodiscard]] CommandParts command_parts(const Layout& placed, CommandAddress address) const;
    [[nodiscard]] Parts control_parts(const Layout& placed) const;
    [[nodiscard]] AnatomyStateSet tab_states(std::size_t tab) const;
    [[nodiscard]] AnatomyStateSet command_states(CommandAddress address) const;
    [[nodiscard]] AnatomyStateSet group_button_states(std::size_t group) const;
    [[nodiscard]] bool group_collapsed(const Layout& placed, std::size_t group) const noexcept;
    [[nodiscard]] ImageId group_image(const RibbonGroupSpec& group) const noexcept;
    [[nodiscard]] const RibbonGroupSpec* selected_group(std::size_t group) const noexcept;
    [[nodiscard]] const RibbonCommandSpec* selected_command(CommandAddress address) const noexcept;
    [[nodiscard]] const std::string& authored_text(const RibbonCommandSpec& command) const noexcept;
    // A borrow of the argument: temporaries are refused (house style §1).
    const std::string& authored_text(RibbonCommandSpec&& command) const = delete;
    [[nodiscard]] std::string command_text(const RibbonCommandSpec& command) const;
    [[nodiscard]] bool command_enabled(const RibbonCommandSpec& command) const;
    [[nodiscard]] bool command_visible(const RibbonCommandSpec& command) const;
    [[nodiscard]] bool command_checked(const RibbonCommandSpec& command) const;
    [[nodiscard]] bool stop_focus_visible(const Stop& stop) const;
    [[nodiscard]] Hit hit_test(const Layout& placed, Point local) const;
    [[nodiscard]] std::vector<Stop> stops() const;
    [[nodiscard]] std::vector<Stop> stops_for(const Layout& placed) const;
    [[nodiscard]] std::vector<Stop> overlay_stops() const;
    [[nodiscard]] Stop resolve_stop(const std::vector<Stop>& all, const Stop& stop) const;
    [[nodiscard]] std::optional<CommandAddress> find_command(std::string_view command_id) const;
    [[nodiscard]] std::optional<std::size_t> find_group(std::string_view group_id) const;
    [[nodiscard]] std::optional<Rect> command_window_bounds(CommandAddress address) const;
    [[nodiscard]] Control::Ptr hosted_control(std::string_view command_id, RibbonCommandKind kind) const;
    [[nodiscard]] Control::Ptr make_hosted(const RibbonCommandSpec& command) const;
    void configure_hosted(const RibbonCommandSpec& command, Control& control) const;
    void paint_groups(Painter& painter, const Layout& placed) const;
    void paint_label_command(Painter& painter, PartPaint& part, const RibbonCommandSpec& spec,
                             const CommandParts& command_part, AnatomyStateSet states) const;
    void paint_tab(Painter& painter, PartPaint& part, const Layout& placed, std::size_t tab) const;
    void append_group_node(SemanticNode& parent, const Layout& placed, std::size_t group, Rect absolute) const;
    [[nodiscard]] std::vector<Group> groups_of(const Layout& placed) const;
    [[nodiscard]] std::vector<CommandItem> commands_of(const Layout& placed) const;

    void select_tab(std::size_t tab, SelectionCause cause = SelectionCause::user);
    void click_tab(std::size_t tab);
    void click_command(CommandAddress address);
    void launch(std::size_t group);
    void scroll_groups(bool forward);
    void set_hot(const Hit& hit);
    // Repaints only the part whose look changed (a command, a tab, the File
    // tab, a group for its launcher or overflow button, a scroll button), in
    // the plane and, for its group, the open group overlay. Hover, press,
    // focus and a menu opening or closing change one part, never the whole
    // ribbon (owner, 2026-11-09).
    void repaint_part(Hit::Kind kind, std::size_t index, CommandAddress command);
    void repaint_stop(const std::optional<Stop>& stop);
    void set_stop(std::optional<Stop> stop);
    void activate_stop(const Stop& stop);
    void press(const Hit& hit, PointerEvent& event);
    void release(const Hit& hit, PointerEvent& event);
    void command_state_changed(std::size_t tab, std::size_t group, std::size_t command);
    void invalidate_views(Dirty dirty);
    void on_overlay_item_invoked(const MenuItemInvocation& invocation);
    void on_overlay_open_changed(bool open);

    // The group overlay (owner decision 27), driven by its content control
    // (ribbon_group_overlay.cpp). `panel` is the group's rectangle in the
    // content control's local coordinates.
    void overlay_paint(Painter& painter, Rect panel) const;
    void overlay_pointer(PointerEvent& event, Rect panel);
    void overlay_key(KeyEvent& event);
    void overlay_focus_changed(bool focused);
    [[nodiscard]] std::vector<SemanticNode> overlay_semantic_children(Rect panel) const;
    bool overlay_semantic_action(std::string_view stable_id, SemanticAction action);
    [[nodiscard]] Size overlay_panel_size() const;
    void move_hosted_to_overlay(std::size_t group);
    void move_hosted_to_ribbon() noexcept;
    void finish_group_overlay() noexcept;
    void on_group_layer_dismissed(OverlayDismissReason reason);
    void on_group_overlay_revoked();

    // The file area (ribbon_file_area.cpp): the two-pane application menu
    // and Backstage.
    [[nodiscard]] bool file_area_open() const noexcept;
    bool show_application_overlay(MenuOpenMode mode);
    void close_application_overlay() noexcept;
    void on_application_overlay_revoked();
    // Opens `item`'s children in the ribbon's menu beside `row` (the row's
    // bounds in `panel`'s coordinates), with the keyboard on the first row
    // when `keyboard`; false when the item has no children or the ribbon
    // has no window.
    bool open_application_submenu(const MenuItemSpec& item, const Control& panel, Rect row, bool keyboard);
    void place_backstage();
    void on_backstage_close_requested();
    void on_backstage_revoked();
    void validate_two_pane(const std::vector<MenuItemSpec>& items, std::size_t recent) const;

    std::vector<RibbonTabSpec> tabs_{};
    // One record per command, indexed [tab][group][command] as tabs_.
    std::vector<std::vector<std::vector<CommandRecord>>> records_{};
    // Every group's layouts, authored or generated, indexed [tab][group].
    std::vector<std::vector<std::vector<RibbonGroupLayout>>> layouts_{};
    std::optional<std::size_t> selected_tab_{};
    // Contextual sets and modes: the sets, their states, each tab's set (none
    // for a core tab), the modes shown, and the core tab chosen last.
    std::vector<RibbonTabSetSpec> tab_sets_{};
    std::vector<TabSetState> set_states_{};
    std::vector<std::optional<std::size_t>> tab_set_of_{};
    std::optional<std::size_t> last_core_tab_{};
    std::vector<MenuItemSpec> application_menu_{};
    std::unique_ptr<ContextMenu> overlay_{};
    SubscriptionToken overlay_invoked_{};
    SubscriptionToken overlay_changed_{};
    // Which menu the overlay shows: the application menu, or a command's.
    bool application_open_{};
    std::optional<CommandAddress> open_command_{};
    Hit hot_{};
    std::optional<CommandAddress> pressed_command_{};
    CommandZone pressed_zone_{CommandZone::none};
    std::optional<std::size_t> pressed_launcher_{};
    // The tab pressed while tabs_activate_on_release holds, until the release.
    std::optional<std::size_t> pressed_tab_{};
    std::optional<std::size_t> pressed_group_{};
    Hit::Kind pressed_scroll_{Hit::Kind::none};
    bool application_pressed_{};
    std::optional<Stop> stop_{};
    bool focused_{};

    // Scaling. The caches are derived state, emptied when scaling_key_ no
    // longer matches; scaling_ has one entry per tab.
    std::uint64_t content_generation_{1U};
    mutable ScalingKey scaling_key_{};
    mutable std::vector<TabScaling> scaling_{};
    mutable RowMetrics row_metrics_{};
    // Scratch for the command rectangles place_group fills while only a
    // width is wanted (state_width, overlay_panel_size); its contents mean
    // nothing between calls, and its capacity is kept for the next.
    mutable std::vector<Rect> scratch_commands_{};
    mutable RibbonScalingCounters counters_{};
    std::size_t first_group_{};
    // Whether the plane scrolls, latched by arrange with scroll_hysteresis.
    bool scroll_latched_{};
    bool plane_hidden_{};

    // The group overlay: its content control and layer (made on first use),
    // the overlay lease and focus scope while open, and the open group with
    // the keyboard's stop in it.
    std::shared_ptr<RibbonGroupOverlay> group_overlay_{};
    std::shared_ptr<AnchoredOverlayLayer> group_layer_{};
    OverlayToken group_overlay_token_{};
    FocusScopeId group_scope_{};
    SubscriptionToken group_layer_dismissed_{};
    SubscriptionToken group_overlay_revoked_{};
    std::optional<std::size_t> overlay_group_{};
    std::optional<Stop> overlay_stop_{};
    bool overlay_focused_{};

    Event<const RibbonCommandClick&> command_clicked_{};
    Event<const RibbonDropDownInvocation&> drop_down_item_invoked_{};
    Event<const RibbonDialogLaunch&> dialog_launcher_clicked_{};
    Event<const RibbonTabChange&> selected_tab_changed_{};
    Event<bool> minimized_changed_{};
    Event<const MenuItemInvocation&> application_menu_item_invoked_{};

    // The file area: recent items, the two-pane menu while open, Backstage.
    std::vector<RibbonRecentItem> recent_items_{};
    std::shared_ptr<RibbonApplicationMenu> application_overlay_{};
    // The panel lifted into the owner's picker Popup while the two-pane
    // menu shows there (application_overlay_ is then its catcher); kept,
    // detached, between shows. `application_face_` is the lifted panel's
    // bounds in the owner's client coordinates.
    std::shared_ptr<RibbonApplicationMenu> application_panel_{};
    PopupLift application_lift_{};
    Rect application_face_{};
    // A submenu item of the two-pane menu ran its command: the menu closes
    // once the ribbon's menu has.
    bool application_submenu_invoked_{};
    OverlayToken application_overlay_token_{};
    FocusScopeId application_scope_{};
    SubscriptionToken application_overlay_revoked_{};
    bool application_overlay_open_{};
    SubscriptionToken backstage_close_{};
    OverlayToken backstage_token_{};
    FocusScopeId backstage_scope_{};
    SubscriptionToken backstage_revoked_{};
    bool backstage_open_{};
    // The theme override the ribbon gave Backstage while open (the
    // application's control): cleared on close if still in force, and never
    // written over an override of the application's own.
    std::shared_ptr<const Theme> backstage_theme_{};

    // The window's theme and presentation notifications while attached
    // (follow_theme); disconnected on detach.
    SubscriptionToken theme_subscription_{};
    SubscriptionToken presentation_subscription_{};
    // The theme and colour scheme follow_theme last followed, so that a
    // presentation change leaving both as they were rebuilds nothing.
    const Theme* followed_theme_{};
    ColourScheme followed_scheme_{ColourScheme::light};
    Event<const RibbonRecentItemInvocation&> recent_item_invoked_{};
    Event<bool> backstage_open_changed_{};
    // property_codemod begin Ribbon (tools/property_families/ribbon.json; generated, do not edit)
private:
    friend struct RibbonApplicationModesCapability<Ribbon>;
    friend struct RibbonDocumentMinimumHeightCapability<Ribbon>;
    friend struct RibbonMinimizedCapability<Ribbon>;
    friend struct RibbonApplicationTextCapability<Ribbon>;
    friend struct RibbonRecentCaptionCapability<Ribbon>;
    friend struct RibbonBackstageCapability<Ribbon>;
    friend struct RibbonFocusRegionCapability<Ribbon>;
    friend struct FontCapability<Ribbon>;
    friend struct RibbonSelectedTabCapability<Ribbon>;
    friend struct RibbonTabsActivateOnReleaseCapability<Ribbon>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(application_modes)& cell, std::uint32_t& incoming);
    void written(const decltype(application_modes)& cell);
    void prepare_write(const decltype(document_minimum_height)& cell, double& incoming);
    void written(const decltype(document_minimum_height)& cell);
    void prepare_write(const decltype(minimized)& cell, bool& incoming);
    void written(const decltype(minimized)& cell);
    void prepare_write(const decltype(application_text)& cell, std::string& incoming);
    void written(const decltype(application_text)& cell);
    void prepare_write(const decltype(recent_caption)& cell, std::string& incoming);
    void written(const decltype(recent_caption)& cell);
    void prepare_write(const decltype(backstage)& cell, std::shared_ptr<BackstageView>& incoming);
    void written(const decltype(backstage)& cell);
    void prepare_write(const decltype(focus_region)& cell, bool& incoming);
    void written(const decltype(focus_region)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void write_view(const decltype(selected_tab)& cell, std::optional<std::size_t> incoming);
    [[nodiscard]] std::optional<std::size_t> read_view(const decltype(selected_tab)& cell) const;
    void prepare_write(const decltype(tabs_activate_on_release)& cell, bool& incoming);
    void written(const decltype(tabs_activate_on_release)& cell);
    // property_codemod end Ribbon
};

} // namespace gui_forms
