To the Holy One, blessed be He, from whom all good things come. We dedicate this work in gratitude for the nourishment that sustains human life, the energy that powers our tools, and the opportunity to weave information into works of use and beauty.

# GUI.Forms

GUI.Forms is a retained-mode, CPU-rendered, cross-platform GUI toolkit for
C++20, in the Windows Forms tradition. You build a tree of controls once and
change it imperatively; the toolkit works out exactly which layout, paint
and hit-test work each change needs. A stable C ABI and C# bindings over it
open the same controls to other languages.

[![CI](https://github.com/malkuth-software/gui_forms/actions/workflows/native.yml/badge.svg)](https://github.com/malkuth-software/gui_forms/actions/workflows/native.yml) [![Docs](https://readthedocs.org/projects/gui-forms/badge/?version=latest)](https://gui-forms.readthedocs.io/) [![Release](https://img.shields.io/github/v/release/malkuth-software/gui_forms?display_name=tag)](https://github.com/malkuth-software/gui_forms/releases) [![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE) ![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg) ![Platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey.svg)

- **Status:** 0.5, the ABI and API seal, is done. Work is now towards
  1.0 (backend): the library, its C ABI and its C# bindings. See the
  [release notes](https://gui-forms.readthedocs.io/en/latest/release/index.html)
  and the [roadmap](https://gui-forms.readthedocs.io/en/latest/dev/roadmap.html).
- **Rendering:** CPU only, by design, with no GPU backend. One
  HarfBuzz/FreeType text engine with bundled fonts draws the same string
  identically on every platform.
- **Controls:** more than 120 stock controls (the Windows Forms set, a
  ribbon, docking, data grids, plots and more), themes written in a
  CSS-syntax theme language, form documents that generate typed window
  classes, and native accessibility on every host.

## Platforms

| Platform | Host | Notes |
|---|---|---|
| macOS 14+ | AppKit | Apple silicon (arm64) |
| Windows 10+ | Win32 | x64; built with LLVM 22 (MSYS2 CLANG64 or clang-cl) |
| Linux | X11 | x64 and arm64; also runs under XWayland. Enable with `-DGUI_FORMS_ENABLE_LINUX_HOST=ON` |

## Quick start

Prerequisites are CMake 3.25+, Ninja, LLVM 22.1 and Python 3.12+. Follow
[Install](https://gui-forms.readthedocs.io/en/latest/user/install.html) for
each platform's preparation. Then build, test and install:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release     -DCMAKE_TOOLCHAIN_FILE=cmake/llvm22.cmake
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix ~/gui-forms-sdk
```

A minimal program against the installed SDK:

```cmake
cmake_minimum_required(VERSION 3.25)
project(hello LANGUAGES CXX)
find_package(GUIForms 0.2 CONFIG REQUIRED)
add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE GUIForms::Application)
```

```cpp
#include <gui_forms/application.hpp>
#include <gui_forms/gui_forms.hpp>

#include <memory>

namespace gf = gui_forms;

int main() {
    gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
    if (!created) return 1;
    std::shared_ptr<gf::Label> label =
        gf::make_control<gf::Label>(gf::StableId("hello.label"), "Hello from GUI.Forms");
    std::unique_ptr<gf::Window> window = std::make_unique<gf::Window>(label, gf::Size{320.0, 120.0});
    gf::ApplicationWindowOptions options{};
    options.title = "Hello";
    const gf::ApplicationResult result = (*created.application).run(std::move(window), options);
    return result.accepted() ? 0 : 1;
}
```

[Your first window](https://gui-forms.readthedocs.io/en/latest/user/first-window.html)
goes further: a form document, a model and an event handler.

## Learn more

- **Documentation:** [gui-forms.readthedocs.io](https://gui-forms.readthedocs.io/),
  with the user guide, the controls by function and the API reference.
- **Template:** [`malkuth-software/template_harness`](https://github.com/malkuth-software/template_harness)
  starts a new application on the SDK.
- **Examples:** [`examples/reference/`](examples/reference/) is a complete
  consumer, built against the installed SDK in CI.
- **Gallery:** the control Gallery ([`demo/`](demo/)) shows every stock
  control. Download it with the Explore and Correspondence previews from
  the [latest release](https://github.com/malkuth-software/gui_forms/releases/latest).

![GUI.Forms Gallery on macOS](docs/images/gallery-macos.png)

<details>
<summary>Stock controls</summary>

<!-- controls:start -->
| Control | Function | Description |
|---|---|---|
| [`Button`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Button.html) | Commands | Push button with accept/cancel dialog results, default-button state, and four visual policies. |
| [`ButtonBase`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ButtonBase.html) |  | Shared retained command state machine for pointer, keyboard, mnemonic, semantic, and dialog activation that Button, CheckBox, RadioButton, and LinkLabel specialize. |
| [`CommandBar`](https://gui-forms.readthedocs.io/api/generated/gui_forms.CommandBar.html) |  | Horizontal row of retained item buttons built from a replaceable CommandItem list that raises one group notification per replacement. |
| [`DropDownButton`](https://gui-forms.readthedocs.io/api/generated/gui_forms.DropDownButton.html) |  | Command button with an explicit disclosure actuator that either routes the whole button to an owned overlay menu or keeps a primary click region beside a trailing split region. |
| [`LinkLabel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.LinkLabel.html) |  | Command presented as underlined link text with retained visited state and link semantics. |
| [`MaskedTextBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.MaskedTextBox.html) | Text editing | Text box that takes input through a mask (digits, letters, literals, case shifts, culture separators); refused input is reported with its position and reason. |
| [`RichTextBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RichTextBox.html) |  | Rich text field whose document is markdown, shown styled in place with its markup dimmed, edited like a multiline text box, formatted from a control bar, and read and written as RTF. |
| [`SearchField`](https://gui-forms.readthedocs.io/api/generated/gui_forms.SearchField.html) |  | Query field: a search mark, an inner TextBox, a clear mark and a busy strip; raises search_requested after typing pauses or on Enter; a search box to assistive technology. |
| [`TextBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TextBox.html) |  | Single- or multiline Unicode editor with directional selection, clipboard and history commands, password projection, bounded input length, suggest and append auto-complete, and ordered text/selection events. |
| [`TokenBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TokenBox.html) |  | Text entry whose accepted entries (Enter, a delimiter or an accepted completion) become removable tokens with stable identities, with keyboard selection and removal, a close glyph per token and suggestions from auto-complete. |
| [`Badge`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Badge.html) | Information display | Small count or dot: the count, or its maximum and + past it, in an accent pill; a zero count hides unless show_zero; dot mode marks something new without a number. |
| [`EmptyState`](https://gui-forms.readthedocs.io/api/generated/gui_forms.EmptyState.html) |  | What a list, page or search shows when it has nothing: a picture, a title, a description and an optional action in a centred column; a group to assistive technology. |
| [`GaugeBase`](https://gui-forms.readthedocs.io/api/generated/gui_forms.GaugeBase.html) |  | Shared read-only gauge authority from the instruments package: a RangeControl value with scale marks, units, decimals and low and high alarm limits, a meter to assistive technology, that LinearGauge and RadialGauge draw. |
| [`InfoBanner`](https://gui-forms.readthedocs.io/api/generated/gui_forms.InfoBanner.html) |  | Message strip across a form or page: a severity icon and keyline, a title and a message, an optional action and a close actuator; a live region to assistive technology. |
| [`Label`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Label.html) |  | Noninteractive text primitive with inherited typography roles, alignment, word wrapping, a bounded line count, and mnemonics. |
| [`LinearGauge`](https://gui-forms.readthedocs.io/api/generated/gui_forms.LinearGauge.html) |  | Bar gauge from the instruments package: the value filling a horizontal or vertical scale with marks, alarm limits marked across it and the value as text at its end. |
| [`PictureBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.PictureBox.html) |  | Noninteractive image presenter over Window-owned generational image resources with the five PictureBoxSizeMode policies and bounded opacity. |
| [`ProgressBar`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ProgressBar.html) |  | Noninteractive range visualization with continuous, segmented, and marquee modes whose motion follows the Window frame scheduler and reduced-motion policy. |
| [`ProgressRing`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ProgressRing.html) |  | Circular progress indicator of forty dots: lit in proportion to value over maximum, or a turning run while indeterminate, on the window's frame clock with reduced motion honoured. |
| [`RadialGauge`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RadialGauge.html) |  | Dial gauge from the instruments package: the value lighting a 270-degree scale of dots with marks, alarm limits and an indicator dot, the value as text in the centre. |
| [`ReviewCard`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ReviewCard.html) |  | Typed review projection over Card that owns title, body, and verdict labels and projects pending/rejected state through ordinary Card laws. |
| [`SevenSegmentDisplay`](https://gui-forms.readthedocs.io/api/generated/gui_forms.SevenSegmentDisplay.html) |  | Read-only seven-segment readout from the instruments package: digits, hexadecimal and the letters a cell can form, decimal points and colons, right-aligned in a fixed or fitted number of cells, unlit segments drawn faintly. |
| [`Spinner`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Spinner.html) |  | Indeterminate busy indicator: a ring of twelve dots, three lit, turning while active on the window's frame clock; still under reduced motion; busy to assistive technology. |
| [`StepIndicator`](https://gui-forms.readthedocs.io/api/generated/gui_forms.StepIndicator.html) |  | Row of numbered steps joined by connectors, completed steps checked and the current step highlighted, a press on a completed step reported for going back. |
| [`SweepScope`](https://gui-forms.readthedocs.io/api/generated/gui_forms.SweepScope.html) |  | Read-only level scope from the instruments package: the level drawn as a trace on a graticule, crossed by a sweep line that is a verified effect program on the window's motion clock. |
| [`TextBlock`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TextBlock.html) |  | CSS text leaf for the layout panels: white space collapsing, UAX #14 line breaking, line-height: normal or a set line height, text-align in both directions, no inset, and first and last baselines for flex, grid and block alignment. |
| [`Toast`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Toast.html) |  | Brief notice above the bottom of a window: a severity mark, a message, an optional action and a close mark, shown by Window::show_toast for a while; a live region to assistive technology. |
| [`BreadcrumbTrail`](https://gui-forms.readthedocs.io/api/generated/gui_forms.BreadcrumbTrail.html) | Selection from a list | Location instrument whose logical path segments are virtual semantic children and whose exact-path TextBox occupies the row only while editing. |
| [`CheckedListBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.CheckedListBox.html) |  | ListBox with an independently retained three-state check model, cancellable pre-change events, and optional check-on-click. |
| [`ChoiceGroup`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ChoiceGroup.html) |  | Row of retained item buttons that selects exactly one ChoiceItem and exposes the selection as a bindable Value. |
| [`ComboBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ComboBox.html) |  | Choice field, noneditable or with an editor (drop_down style), with placeholder projection, keyboard cycling, a transient ListBox overlay, and auto-complete from its items or a custom list. |
| [`CorrespondenceView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.CorrespondenceView.html) |  | Variable-height virtual list with independent keyboard focus, delayed pointer inspection, and one persistent pinned item. |
| [`DataGridView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.DataGridView.html) |  | Grid of text, check box, combo box, button, image and link columns over unbound, virtual or BindingSource rows identified by stable row IDs, with cell or row selection kept by ID, in-place editing and only visible rows realized. |
| [`ListBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ListBox.html) |  | Virtual-row selector with single, multi-simple, and multi-extended selection policies, stable item identities, and scroll-window projection. |
| [`ListView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ListView.html) |  | Item list with details, list, small-icon and large-icon views, check boxes, column headers that report clicks and resize, and a virtual mode that realizes only visible items. |
| [`LookupComboBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.LookupComboBox.html) |  | Multi-column lookup: a drop-down field choosing a row by key and showing one of its cells, with a headed list of every column filtered by typing. |
| [`ObjectView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ObjectView.html) |  | Stable-ID virtual object collection with icon and details projections that owns no per-item controls. |
| [`TreeListView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TreeListView.html) |  | Tree with tabular columns: nodes kept by stable ID, expand and collapse, sorting within siblings, resizable and movable columns, and only visible rows realized. |
| [`TreeView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TreeView.html) |  | Flat-model retained tree that paints and publishes only visible rows while item identity and expansion state stay stable for unrealized descendants. |
| [`CheckBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.CheckBox.html) | Value setting | Two- or three-state selection control with optional automatic cycling, three indicator families, and ordered state events. |
| [`ColorValueEditor`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ColorValueEditor.html) |  | Compound editor that delegates editing to a real TextBox and adds a checker-backed swatch with canonical #RRGGBBAA conversion. |
| [`Dial`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Dial.html) |  | Rotary knob from the instruments package: a RangeControl value set by turning, dragging or the TrackBar keys over a 270-degree scale, a slider to assistive technology. |
| [`DomainUpDown`](https://gui-forms.readthedocs.io/api/generated/gui_forms.DomainUpDown.html) |  | Text field with up and down arrows stepping through an ordered string list; the selection follows the text, with optional wrapping, sorting and a read-only field. |
| [`FlagsValueEditor`](https://gui-forms.readthedocs.io/api/generated/gui_forms.FlagsValueEditor.html) |  | Compact editor for independent-bit enumeration values that opens a real CheckedListBox overlay under Window overlay and focus-scope leases. |
| [`InstrumentRack`](https://gui-forms.readthedocs.io/api/generated/gui_forms.InstrumentRack.html) |  | Bounded retained rack of compact editing modules that reconciles stable module and field identities across model replacement. |
| [`NumericUpDown`](https://gui-forms.readthedocs.io/api/generated/gui_forms.NumericUpDown.html) |  | Composite numeric editor over an owned TextBox and source-private spin buttons with ordered ranges, bounded step and precision, and decimal or hexadecimal formatting. |
| [`PropertyGrid`](https://gui-forms.readthedocs.io/api/generated/gui_forms.PropertyGrid.html) |  | Metadata-driven inspector and editor over one or more selected Controls that projects descriptors into a PropertyList. |
| [`PropertyList`](https://gui-forms.readthedocs.io/api/generated/gui_forms.PropertyList.html) |  | Settings surface over caller-authored stable groups and rows that owns stock or replacement child editors, reset buttons, and one vertical scroll plane. |
| [`RadioButton`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RadioButton.html) |  | Retained checked state with container-scoped named exclusion groups, optional automatic checking, and radio semantics. |
| [`RangeControl`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RangeControl.html) |  | Nonvisual retained authority for ordered bounds, a constrained value, small/large changes, and orientation that TrackBar, ProgressBar, and ScrollBar specialize. |
| [`ToggleSwitch`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ToggleSwitch.html) |  | On/off switch for a setting that takes effect at once: a rounded track and thumb, a label and optional state text; a switch to assistive technology. |
| [`TrackBar`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TrackBar.html) |  | Slider over RangeControl with bounded tick frequency, absolute pointer positioning, keyboard movement, and slider semantics. |
| [`DateTimePicker`](https://gui-forms.readthedocs.io/api/generated/gui_forms.DateTimePicker.html) | Date setting | Civil date/time field with validated bounds, four formatting policies, optional check state, and calendar or spin presentation. |
| [`MonthCalendar`](https://gui-forms.readthedocs.io/api/generated/gui_forms.MonthCalendar.html) |  | One or more month pages for selecting a date or a run of dates, with a date range, today marker, bold dates, week numbers and keyboard navigation. |
| [`Card`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Card.html) | Grouping | Header/body/footer composition that owns each installed section as an ordinary visual child and returns the detached predecessor on replacement. |
| [`ContainerControl`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ContainerControl.html) |  | ScrollableControl with descendant-scoped active-control authority and inherited validation policy. |
| [`ExpandableSections`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ExpandableSections.html) |  | Stack of captioned sections whose content controls expand and collapse individually or in single-open mode. |
| [`GroupBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.GroupBox.html) |  | Titled Panel with mnemonic focus routing, caption-aware border painting, and group semantics. |
| [`MaterialPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.MaterialPanel.html) |  | Content-agnostic container painted with a SurfaceMaterial. |
| [`NavigationPane`](https://gui-forms.readthedocs.io/api/generated/gui_forms.NavigationPane.html) |  | Outlook-style navigation pane over its own PageSet: header with collapse, the selected page's control, a grip stepping the large buttons, an overflow icon strip with a configure menu (more, fewer, hidden pages, options event), a collapsed bar with a turned caption opening the content in an overlay, and a state record the program saves. |
| [`PageStrip`](https://gui-forms.readthedocs.io/api/generated/gui_forms.PageStrip.html) |  | The tab headers of a borrowed PageSet in one row on any edge (turned labels on the sides): content-sized, fixed or fill tabs with minimum and maximum widths, scroll and list buttons or shrink then a counted list when they overflow, close buttons with frozen widths, modified dots, icon-only pinned tabs, live drag reorder with a claimable drag start, automatic or manual keyboard selection, mnemonics, and tab-list semantics without press. |
| [`PageView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.PageView.html) |  | A PageStrip over the view's own PageSet with the selected page's control below it, in the strip or document preset: per-page focus memory, late content requested on first selection, close requests decided by the owner with Ctrl+F4 and middle click, Ctrl+PageDown and a Ctrl+Tab policy for nesting and docking, optional F6 region, and tab panels linked from their tabs. |
| [`Panel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Panel.html) |  | Scroll-capable surface primitive with theme-role rendering, explicit background and border overrides, and optional named-group semantics. |
| [`ScaledGroupBox`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ScaledGroupBox.html) |  | GroupBox whose children occupy stable design-space slots arranged proportionally to the current content bounds. |
| [`TabControl`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TabControl.html) |  | Owner of TabPage lifetime and selection with per-page focus restoration, tabs on an internal PageStrip (caption-sized, fill or fixed by size mode, scroll and list buttons when they overflow), four header alignments, three appearances, tab images from an ImageList by key or index, and virtual tab semantics. |
| [`TabPage`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TabPage.html) |  | Panel surface owned by a TabControl with bounded caption text and page semantics. |
| [`UserControl`](https://gui-forms.readthedocs.io/api/generated/gui_forms.UserControl.html) |  | Authored-container base that distinguishes current attachment from first committed load and counts attachment epochs. |
| [`Wizard`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Wizard.html) |  | Guided sequence of WizardPage children shown one at a time, with the page's title and description, a step indicator, Back, Next or Finish and Cancel, and a cancellable page_changing. |
| [`WizardPage`](https://gui-forms.readthedocs.io/api/generated/gui_forms.WizardPage.html) |  | One page of a Wizard: a Panel with the title and description the wizard shows and can_advance, the gate on its Next button. |
| [`BlockPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.BlockPanel.html) | Layout | CSS block formatting context: block items stacked with collapsing margins and auto horizontal margins, and runs of inline items broken into line boxes with text-align, a line strut and vertical-align, nested panels sharing margins, and right-to-left lines. |
| [`CommandOverflowPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.CommandOverflowPanel.html) |  | Horizontal command-group container that owns only layout collapse and the overflow actuator's availability while command authority stays with the consumer. |
| [`DockNavigator`](https://gui-forms.readthedocs.io/api/generated/gui_forms.DockNavigator.html) |  | The two-list Ctrl+Tab switcher of docking: active tool windows beside active files in most-recently-active order, stepping with Tab and the arrows and choosing on the modifier's release, Enter or a press. |
| [`DockSite`](https://gui-forms.readthedocs.io/api/generated/gui_forms.DockSite.html) |  | The docked part of a DockWorkspace in one window: binary splits with 3-unit seams moved by pointer and keyboard within minimums, captioned tool stacks and the document area built from PageView, root-level pins that auto-hide a stack to side tabs on the window's edges with sliding in-window flyouts, the position menu, F6 regions, the Ctrl+Tab navigator, Escape and Shift+Escape, a maximized document area, tool stacks collapsing by priority behind side tabs when the window is too small, and dragging in one window: caption and tab drags with a postage-stamp miniature, position-sensitive targets with quiet tiles over the hovered pane and global edge targets, Ctrl suppression, snap-back, and the keyboard move mode. |
| [`FlexPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.FlexPanel.html) |  | CSS Flexbox container: rows or columns, wrapping lines, grow and shrink with frozen minimums and maximums, justify and align, auto margins, order and right-to-left mirroring. |
| [`FlowLayoutPanelCompat`](https://gui-forms.readthedocs.io/api/generated/gui_forms.FlowLayoutPanelCompat.html) |  | For Windows Forms migration, the Windows Forms FlowLayoutPanel: four flow directions, optional wrapping, bounded spacing, per-child margins and stable flow breaks; use FlexPanel for new work. |
| [`GridPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.GridPanel.html) |  | CSS Grid container: explicit and implicit tracks, line-based and automatic placement with dense packing, named areas, intrinsic, fixed and flexible track sizing, auto-fill and auto-fit repeats, gaps, box alignment with baselines, and right-to-left mirroring. |
| [`MasterDetailView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.MasterDetailView.html) |  | Content-agnostic master/detail shell with responsive presentation, a draggable and keyboard-operable splitter, and focus transfer on collapse. |
| [`ResponsiveTrackPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ResponsiveTrackPanel.html) |  | Track-based container that places each child in one authored track and collapses tracks as an effective-layout state without changing author visibility. |
| [`ScaledPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ScaledPanel.html) |  | Panel that maps caller-authored design-space child rectangles into the current content bounds while retaining slot identity independently of child order. |
| [`SplitContainer`](https://gui-forms.readthedocs.io/api/generated/gui_forms.SplitContainer.html) |  | Two-pane allocation container with bounded splitter distance, fixed-pane resize preservation, pointer and keyboard seam input, and collapse. |
| [`Splitter`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Splitter.html) |  | Windows Forms migration facade: a bar docked after a docked sibling that resizes it by dragging, within MinSize and MinExtra, over the dock layout; new code uses SplitContainer. |
| [`SplitterPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.SplitterPanel.html) |  | Allocated pane surface owned by SplitContainer that paints its own background from committed bounds. |
| [`TableLayoutPanelCompat`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TableLayoutPanelCompat.html) |  | For Windows Forms migration, the Windows Forms TableLayoutPanel: bounded rows and columns, absolute/percent/auto tracks, explicit or automatic placement and row/column spans; use GridPanel for new work. |
| [`HScrollBar`](https://gui-forms.readthedocs.io/api/generated/gui_forms.HScrollBar.html) | Scrolling | Explicit horizontal ScrollBar leaf type. |
| [`ScrollableControl`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ScrollableControl.html) |  | Retained scrolling state machine with overflow discovery, viewport rectangles, two axis policies, wheel/track/thumb interaction, and virtual scrollbar semantics. |
| [`ScrollBar`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ScrollBar.html) |  | RangeControl with bounded button and thumb geometry, proportional thumb allocation, drag and track interaction, and retained auto-repeat. |
| [`VScrollBar`](https://gui-forms.readthedocs.io/api/generated/gui_forms.VScrollBar.html) |  | Explicit vertical ScrollBar leaf type. |
| [`BackstageView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.BackstageView.html) | Menus and commands | The File tab's full page: a column of fast commands and topics beside the selected topic's page, opened by a Ribbon over everything below its tab row. |
| [`BindingNavigator`](https://gui-forms.readthedocs.io/api/generated/gui_forms.BindingNavigator.html) |  | ToolStrip that moves through a BindingSource with first, previous, position, count, next, last, add and delete, each a shared Command enabled by the source's position, count and permissions. |
| [`ColorCommand`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ColorCommand.html) |  | Value command of a colour picker: shares identity and state with a Command and carries a colour, or Automatic, and the recent colours. |
| [`Command`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Command.html) |  | Shared retained authority for menu, shortcut, context, semantic, and test surfaces with validated UTF-8 state and enabled-only invocation. |
| [`ContextMenu`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ContextMenu.html) |  | Overlay-menu controller whose rows are temporary overlay presentations over stable MenuItemSpec identities and bound Commands. |
| [`FontCommand`](https://gui-forms.readthedocs.io/api/generated/gui_forms.FontCommand.html) |  | Value command of a font control: shares identity and state with a Command and carries a font family and size. |
| [`GalleryCommand`](https://gui-forms.readthedocs.io/api/generated/gui_forms.GalleryCommand.html) |  | Value command of a gallery: shares identity and state with a Command, carries the chosen item, and raises preview and cancel-preview events. |
| [`MenuStrip`](https://gui-forms.readthedocs.io/api/generated/gui_forms.MenuStrip.html) |  | Top-level application menu bar whose active menu is presented by ContextMenu as a separate overlay root bound to shared Command instances. |
| [`QuickAccessToolbar`](https://gui-forms.readthedocs.io/api/generated/gui_forms.QuickAccessToolbar.html) |  | Ribbon Quick Access Toolbar: a ToolStrip of flat command buttons with a customize menu that shows or hides each declared command and records the toolbar's placement above or below the ribbon. |
| [`Ribbon`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Ribbon.html) |  | Office 2010-style ribbon: File tab and core tabs over a command plane of captioned groups with dialog launchers and command records bound to shared Commands, hosted editors and galleries, adaptive scaling by group layouts and a reduction order (collapsed groups open in an overlay, scrolling below the narrowest layout), the document-area hide rule, contextual tab sets tinted from the accent ramp with built-in selection rules, application modes, and minimizing by a click on the selected tab. |
| [`RibbonColorPicker`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RibbonColorPicker.html) |  | Ribbon drop-down colour picker: a split button that applies its colour, and a palette overlay of Automatic, palette and recent colours, through a ColorCommand. |
| [`RibbonFontControl`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RibbonFontControl.html) |  | Ribbon font control: a family list and a size list choosing one font through a FontCommand, hosted in a Ribbon or standalone. |
| [`RibbonGallery`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RibbonGallery.html) |  | Ribbon gallery of image items in categories, in the ribbon with scroll and more buttons or as a drop-down overlay, with live preview through a GalleryCommand and only visible rows realized. |
| [`StatusStrip`](https://gui-forms.readthedocs.io/api/generated/gui_forms.StatusStrip.html) |  | Status bar laid out as a ToolStrip that never overflows, with spring labels sharing the free width and a sizing grip. |
| [`ToolStrip`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ToolStrip.html) |  | Command strip of buttons, labels, separators, drop-down and split buttons laid out as an item system, with an overflow menu for items that do not fit. |
| [`ToolStripContainer`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ToolStripContainer.html) |  | Four ToolStripPanels around a content panel; the edge panels take the room their strips need and the content takes the rest. |
| [`ToolStripPanel`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ToolStripPanel.html) |  | Band along one edge that stacks joined ToolStrips in rows (top, bottom) or vertical columns (left, right), as thick as its strips. |
| [`AboutDialog`](https://gui-forms.readthedocs.io/api/generated/gui_forms.AboutDialog.html) | Dialogs and overlays | An application's About box: a TaskDialog preset with the information icon, the product and version, description and copyright, credits behind an expander, and OK. |
| [`AnchoredOverlayLayer`](https://gui-forms.readthedocs.io/api/generated/gui_forms.AnchoredOverlayLayer.html) |  | Full-client overlay that owns one retained overlay content subtree, re-resolves anchored bounds on resize, and reports click-away and Escape requests. |
| [`Callout`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Callout.html) |  | Non-modal teaching callout at a target control: a card with a title, wrapped text, a close mark and an optional action, placed below or above the target in an overlay, with optional light dismiss. |
| [`ColorDialog`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ColorDialog.html) |  | The host's dialog for choosing a color, opaque unless alpha is allowed, shown modally through HostServices. |
| [`FolderBrowserDialog`](https://gui-forms.readthedocs.io/api/generated/gui_forms.FolderBrowserDialog.html) |  | The host's dialog for choosing a folder, with a description and a starting folder, shown modally through HostServices. |
| [`FontDialog`](https://gui-forms.readthedocs.io/api/generated/gui_forms.FontDialog.html) |  | In-window owned dialog choosing a bundled face by role, a style the face has, and a size, with a preview and Apply, OK and Cancel; returns a FontSpec. |
| [`ModalLayer`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ModalLayer.html) |  | In-window modal layer: any control shown centred over its owner's window, taking every press outside it and holding focus, closed with a DialogResult through a cancellable closing. |
| [`RibbonToolTip`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RibbonToolTip.html) |  | Nonvisual provider of enhanced tooltips on controls and ToolStrip items: title, description, shortcut, image, a disabled command's reason, and an F1 help topic reported as an event. |
| [`SaveFileDialog`](https://gui-forms.readthedocs.io/api/generated/gui_forms.SaveFileDialog.html) |  | The host's dialog for choosing where to save a file, with a Windows Forms filter string, a default extension, an overwrite prompt and a cancellable file_ok, shown modally through HostServices. |
| [`TaskDialog`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TaskDialog.html) |  | In-window owned message dialog of TaskDialogPage pages: heading, text, icon, standard and custom buttons, radio buttons, a verification check box, an expander, a footnote and a progress bar, with navigation between pages. |
| [`ThreadExceptionDialog`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ThreadExceptionDialog.html) |  | The dialog for an exception the application did not handle: a TaskDialog preset with the error icon, the message, details behind an expander, and Continue and Quit, returned as cancel and abort. |
| [`ToolTip`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ToolTip.html) |  | Nonvisual provider that maps Controls to tooltip text and presents a retained, input-transparent overlay owned by each target. |
| [`DrawingSurface`](https://gui-forms.readthedocs.io/api/generated/gui_forms.DrawingSurface.html) | Drawing | Owner-render surface configured through the renderer-neutral Painter vocabulary without a Control subclass. |
| [`LiveSurface`](https://gui-forms.readthedocs.io/api/generated/gui_forms.LiveSurface.html) |  | Latest-frame surface where a producer publishes complete frame candidates and painters acquire only the newest immutable one. |
| [`RasterCanvas`](https://gui-forms.readthedocs.io/api/generated/gui_forms.RasterCanvas.html) |  | Viewport over an owned GUI.Drawing bitmap that handles presentation, coordinate transforms, resource synchronization, and local invalidation. |
| [`SubSurfaceHost`](https://gui-forms.readthedocs.io/api/generated/gui_forms.SubSurfaceHost.html) |  | Region control that runs one threaded sub-surface with its own retained tree, sizes and reconfigures it, composes its LiveSurface frames into the window, and forwards input and keyboard focus to it. |
| [`EasingPreview`](https://gui-forms.readthedocs.io/api/generated/gui_forms.EasingPreview.html) | Diagnostics | Animation and easing conformance surface over an owned AnimationTimeline with configurable tracks and motion policy. |
| [`MetricsView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.MetricsView.html) |  | View over the structured Window metrics snapshot. |
| [`TypographySpecimen`](https://gui-forms.readthedocs.io/api/generated/gui_forms.TypographySpecimen.html) |  | Typography review control that draws one committed text run with its logical line box, resolved baseline, and provider status. |
| [`VisualInspectorOverlay`](https://gui-forms.readthedocs.io/api/generated/gui_forms.VisualInspectorOverlay.html) |  | Transparent companion overlay that draws a target's arranged, visual, and effective clipped bounds without altering hit testing. |
| [`VisualInspectorView`](https://gui-forms.readthedocs.io/api/generated/gui_forms.VisualInspectorView.html) |  | Presentation of the Window visual-inspection snapshot for demoboards and local diagnostics. |
| [`BackgroundWorker`](https://gui-forms.readthedocs.io/api/generated/gui_forms.BackgroundWorker.html) | Components | Runs one work function at a time on a gui_forms Worker thread and delivers progress and completion on its window's UI thread through the dispatcher, with cooperative cancellation. |
| [`Binding`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Binding.html) |  | Connects one Control property to one BindingSource field with validated conversion, update timing, and error routing. |
| [`BindingContext`](https://gui-forms.readthedocs.io/api/generated/gui_forms.BindingContext.html) |  | Window-scoped weak membership and disposal subscriptions for BindingSources. |
| [`BindingSource`](https://gui-forms.readthedocs.io/api/generated/gui_forms.BindingSource.html) |  | Owns stable rows, currency, edit snapshots, list and error events, and the active Bindings for one Window. |
| [`DockWorkspace`](https://gui-forms.readthedocs.io/api/generated/gui_forms.DockWorkspace.html) |  | Docking workspace component: owns the DockLayout, the panel registrations with lazy factories and their content, and the active panel and active document tracks with their histories, named on the window's command routing chain; asks the program before documents close at the user's request (each or combined, cancellable and deferrable); saves and restores the layout on live windows, and keeps named layouts for the program to save. |
| [`ErrorProvider`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ErrorProvider.html) |  | Validation adornment provider that projects error text onto the target semantic node and gives each visible target an independent overlay glyph. |
| [`HelpProvider`](https://gui-forms.readthedocs.io/api/generated/gui_forms.HelpProvider.html) |  | Policy-free help provider that retains authored metadata and routes F1 and pointer help requests to the consumer. |
| [`ImageList`](https://gui-forms.readthedocs.io/api/generated/gui_forms.ImageList.html) |  | Ordered, keyed collection of PNG-sourced Window image resources with density and state variants and atomic mutation. |
| [`NotifyIcon`](https://gui-forms.readthedocs.io/api/generated/gui_forms.NotifyIcon.html) |  | Icon in the desktop's notification area (Windows tray, macOS status item, X11 system tray) with a tooltip, clicks, a native context menu and notification text, each refused explicitly where the host lacks the capability. |
| [`OpenFileDialog`](https://gui-forms.readthedocs.io/api/generated/gui_forms.OpenFileDialog.html) |  | The host's dialog for choosing existing files, with a Windows Forms filter string, multiple selection and a cancellable file_ok, shown modally through HostServices. |
| [`SemanticFeedback`](https://gui-forms.readthedocs.io/api/generated/gui_forms.SemanticFeedback.html) |  | Turns one-shot application transitions into bounded, traceable host cues. |
| [`Timer`](https://gui-forms.readthedocs.io/api/generated/gui_forms.Timer.html) |  | Timer whose callbacks are serialized with input, layout, and paint on the owning Window's UI thread and whose late ticks are coalesced. |
<!-- controls:end -->

</details>

## Contributing

CI must pass on every platform. C++ follows the
[house style](https://github.com/malkuth-software/notes/blob/main/HOUSE_STYLE.md);
run `python3 -B tools/check_house_style.py include src demo tests examples tools file_manager_demoboard`
before you push. See the
[contributor's guide](https://gui-forms.readthedocs.io/en/latest/dev/index.html).

## License

[MIT](LICENSE). Bundled fonts carry their own notices in
[`assets/fonts/ATTRIBUTION.md`](assets/fonts/ATTRIBUTION.md).
