# Member manifest

Generated from `manifests/members.json` by `python3 tools/control_registry.py render`. Do not edit.

Every GUI.Forms control and every property, event and method on each, against its Windows Forms (.NET 8 or later) counterpart: supported, deferred or excluded, or unclassified until someone decides. Each control's API reference page lists its members; this page counts them. `python3 tools/control_registry.py members --check` fails when the manifest and the registry disagree.

## Totals

| Supported | Deferred | Excluded | Unclassified | GUI.Forms-only |
|---|---|---|---|---|
| 1938 | 13 | 315 | 3488 | 2046 |

## GUI.Forms types

| Type | Windows Forms | Supported | Deferred | Excluded | Unclassified | GUI.Forms-only |
|---|---|---|---|---|---|---|
| `Control` | `Control` | 186 | 0 | 5 | 345 | 179 |
| `Component` | GUI.Forms-only | 7 | 0 | 0 | 2 | 9 |
| `ButtonBase` | `ButtonBase` | 17 | 0 | 0 | 35 | 21 |
| `Button` | `Button` | 6 | 0 | 0 | 6 | 5 |
| `DropDownButton` | `ToolStripSplitButton` | 6 | 0 | 0 | 18 | 5 |
| `LinkLabel` | `LinkLabel` | 1 | 0 | 0 | 17 | 0 |
| `CommandBar` | `ToolStrip` | 5 | 0 | 0 | 73 | 3 |
| `TextBox` | `TextBox` | 30 | 0 | 0 | 25 | 30 |
| `TokenBox` | GUI.Forms-only | 12 | 0 | 0 | 1 | 13 |
| `SearchField` | GUI.Forms-only | 6 | 0 | 0 | 5 | 11 |
| `MaskedTextBox` | `MaskedTextBox` | 14 | 0 | 0 | 39 | 6 |
| `RichTextBox` | `RichTextBox` | 31 | 0 | 0 | 57 | 20 |
| `Label` | `Label` | 5 | 0 | 1 | 34 | 7 |
| `TextBlock` | GUI.Forms-only | 2 | 0 | 0 | 6 | 8 |
| `Badge` | GUI.Forms-only | 3 | 0 | 0 | 4 | 7 |
| `InfoBanner` | GUI.Forms-only | 3 | 0 | 0 | 5 | 8 |
| `EmptyState` | GUI.Forms-only | 2 | 0 | 0 | 3 | 5 |
| `Toast` | GUI.Forms-only | 4 | 0 | 0 | 1 | 5 |
| `PictureBox` | `PictureBox` | 5 | 0 | 0 | 36 | 7 |
| `ProgressBar` | `ProgressBar` | 8 | 0 | 0 | 35 | 11 |
| `ProgressRing` | GUI.Forms-only | 5 | 0 | 0 | 3 | 8 |
| `Spinner` | GUI.Forms-only | 2 | 0 | 0 | 2 | 4 |
| `SweepScope` | GUI.Forms-only | 2 | 0 | 0 | 3 | 5 |
| `Sparkline` | GUI.Forms-only | 6 | 0 | 0 | 4 | 10 |
| `PlotView` | GUI.Forms-only | 18 | 0 | 0 | 11 | 29 |
| `PlotAxisItem` | GUI.Forms-only | 2 | 0 | 0 | 8 | 10 |
| `PlotSeriesItem` | GUI.Forms-only | 8 | 0 | 0 | 19 | 27 |
| `PlotAnnotationItem` | GUI.Forms-only | 0 | 0 | 0 | 4 | 4 |
| `ListBox` | `ListBox` | 11 | 0 | 1 | 47 | 10 |
| `CheckedListBox` | `CheckedListBox` | 12 | 0 | 0 | 18 | 6 |
| `ComboBox` | `ComboBox` | 11 | 0 | 1 | 52 | 9 |
| `TreeView` | `TreeView` | 10 | 0 | 1 | 75 | 9 |
| `ObjectView` | `ListView` | 20 | 0 | 1 | 107 | 18 |
| `ListView` | `ListView` | 49 | 0 | 1 | 98 | 38 |
| `TreeListView` | GUI.Forms-only | 31 | 0 | 0 | 4 | 35 |
| `DataGridView` | `DataGridView` | 130 | 0 | 0 | 329 | 79 |
| `ChoiceGroup` | GUI.Forms-only | 6 | 0 | 0 | 3 | 9 |
| `BreadcrumbTrail` | GUI.Forms-only | 12 | 0 | 0 | 2 | 14 |
| `CheckBox` | `CheckBox` | 4 | 0 | 0 | 14 | 3 |
| `RadioButton` | `RadioButton` | 4 | 0 | 0 | 11 | 3 |
| `ToggleSwitch` | GUI.Forms-only | 4 | 0 | 0 | 3 | 7 |
| `RangeControl` | GUI.Forms-only | 8 | 0 | 0 | 5 | 13 |
| `TrackBar` | `TrackBar` | 12 | 0 | 0 | 30 | 8 |
| `ValueBar` | GUI.Forms-only | 18 | 0 | 0 | 19 | 37 |
| `NumericUpDown` | `NumericUpDown` | 7 | 0 | 0 | 16 | 6 |
| `DomainUpDown` | `DomainUpDown` | 5 | 0 | 0 | 12 | 5 |
| `ColorValueEditor` | GUI.Forms-only | 3 | 0 | 0 | 3 | 6 |
| `FlagsValueEditor` | GUI.Forms-only | 1 | 0 | 0 | 6 | 7 |
| `PropertyGrid` | `PropertyGrid` | 19 | 0 | 0 | 74 | 19 |
| `PropertyList` | GUI.Forms-only | 16 | 0 | 0 | 4 | 20 |
| `DateTimePicker` | `DateTimePicker` | 6 | 0 | 0 | 46 | 4 |
| `MonthCalendar` | `MonthCalendar` | 18 | 0 | 0 | 44 | 5 |
| `Panel` | `Panel` | 8 | 0 | 0 | 8 | 7 |
| `GroupBox` | `GroupBox` | 3 | 0 | 0 | 20 | 1 |
| `ScaledGroupBox` | GUI.Forms-only | 3 | 0 | 0 | 2 | 5 |
| `MaterialPanel` | GUI.Forms-only | 1 | 0 | 0 | 0 | 1 |
| `Card` | GUI.Forms-only | 10 | 0 | 0 | 1 | 11 |
| `ExpandableSections` | GUI.Forms-only | 5 | 0 | 0 | 0 | 5 |
| `ContainerControl` | `ContainerControl` | 9 | 0 | 0 | 11 | 4 |
| `UserControl` | `UserControl` | 5 | 0 | 0 | 7 | 4 |
| `TabControl` | `TabControl` | 16 | 1 | 1 | 39 | 12 |
| `PageStrip` | GUI.Forms-only | 30 | 0 | 0 | 4 | 34 |
| `PageView` | GUI.Forms-only | 8 | 0 | 0 | 1 | 9 |
| `NavigationPane` | GUI.Forms-only | 17 | 0 | 0 | 3 | 20 |
| `DockSite` | GUI.Forms-only | 44 | 0 | 0 | 7 | 51 |
| `DockNavigator` | GUI.Forms-only | 10 | 0 | 0 | 4 | 14 |
| `TabPage` | `TabPage` | 4 | 0 | 0 | 16 | 0 |
| `FlexPanel` | GUI.Forms-only | 5 | 0 | 0 | 2 | 7 |
| `GridPanel` | GUI.Forms-only | 11 | 0 | 0 | 3 | 14 |
| `BlockPanel` | GUI.Forms-only | 7 | 0 | 0 | 4 | 11 |
| `FlowLayoutPanelCompat` | `FlowLayoutPanel` | 3 | 0 | 0 | 9 | 7 |
| `TableLayoutPanelCompat` | `TableLayoutPanel` | 13 | 0 | 0 | 26 | 13 |
| `ResponsiveTrackPanel` | GUI.Forms-only | 6 | 0 | 0 | 5 | 11 |
| `ScaledPanel` | GUI.Forms-only | 3 | 0 | 0 | 2 | 5 |
| `SplitContainer` | `SplitContainer` | 12 | 0 | 0 | 37 | 14 |
| `SplitterPanel` | `SplitterPanel` | 4 | 0 | 0 | 19 | 0 |
| `Splitter` | `Splitter` | 9 | 0 | 0 | 19 | 5 |
| `CommandOverflowPanel` | GUI.Forms-only | 2 | 0 | 0 | 4 | 6 |
| `MasterDetailView` | GUI.Forms-only | 10 | 0 | 0 | 2 | 12 |
| `ScrollableControl` | `ScrollableControl` | 16 | 0 | 0 | 16 | 13 |
| `ScrollBar` | `ScrollBar` | 12 | 0 | 0 | 24 | 6 |
| `HScrollBar` | `HScrollBar` | 1 | 0 | 0 | 0 | 0 |
| `VScrollBar` | `VScrollBar` | 1 | 0 | 0 | 1 | 0 |
| `MenuStrip` | `MenuStrip` | 9 | 0 | 0 | 9 | 7 |
| `ToolStrip` | `ToolStrip` | 15 | 0 | 0 | 77 | 17 |
| `RibbonFontControl` | GUI.Forms-only | 9 | 0 | 0 | 1 | 10 |
| `RibbonColorPicker` | GUI.Forms-only | 7 | 0 | 0 | 2 | 9 |
| `BackstageView` | GUI.Forms-only | 3 | 0 | 0 | 2 | 5 |
| `RibbonGallery` | GUI.Forms-only | 18 | 0 | 0 | 7 | 25 |
| `StatusStrip` | `StatusStrip` | 1 | 0 | 0 | 10 | 0 |
| `QuickAccessToolbar` | GUI.Forms-only | 10 | 0 | 0 | 0 | 10 |
| `ToolStripPanel` | `ToolStripPanel` | 8 | 0 | 0 | 18 | 5 |
| `ToolStripContainer` | `ToolStripContainer` | 16 | 0 | 0 | 25 | 16 |
| `BindingNavigator` | `BindingNavigator` | 8 | 0 | 0 | 18 | 8 |
| `Ribbon` | GUI.Forms-only | 38 | 0 | 0 | 6 | 44 |
| `ContextMenu` | `ContextMenuStrip` | 9 | 0 | 0 | 1 | 5 |
| `DockWorkspace` | GUI.Forms-only | 39 | 0 | 0 | 16 | 55 |
| `Command` | GUI.Forms-only | 15 | 0 | 0 | 4 | 19 |
| `FontCommand` | GUI.Forms-only | 5 | 0 | 0 | 2 | 7 |
| `ColorCommand` | GUI.Forms-only | 7 | 0 | 0 | 1 | 8 |
| `GalleryCommand` | GUI.Forms-only | 11 | 0 | 0 | 1 | 12 |
| `AnchoredOverlayLayer` | GUI.Forms-only | 3 | 0 | 0 | 1 | 4 |
| `FontDialog` | `FontDialog` | 16 | 0 | 0 | 19 | 15 |
| `TaskDialog` | `TaskDialog` | 19 | 0 | 0 | 4 | 19 |
| `ThreadExceptionDialog` | `ThreadExceptionDialog` | 7 | 0 | 0 | 2 | 3 |
| `SevenSegmentDisplay` | GUI.Forms-only | 3 | 0 | 0 | 4 | 7 |
| `GaugeBase` | GUI.Forms-only | 4 | 0 | 0 | 5 | 9 |
| `LinearGauge` | GUI.Forms-only | 0 | 0 | 0 | 0 | 0 |
| `RadialGauge` | GUI.Forms-only | 0 | 0 | 0 | 0 | 0 |
| `Dial` | GUI.Forms-only | 1 | 0 | 0 | 4 | 5 |
| `StepIndicator` | GUI.Forms-only | 4 | 0 | 0 | 2 | 6 |
| `Wizard` | GUI.Forms-only | 11 | 0 | 0 | 14 | 25 |
| `WizardPage` | GUI.Forms-only | 0 | 0 | 0 | 4 | 4 |
| `Callout` | GUI.Forms-only | 6 | 0 | 0 | 8 | 14 |
| `ModalLayer` | GUI.Forms-only | 6 | 0 | 0 | 3 | 9 |
| `LookupComboBox` | GUI.Forms-only | 9 | 0 | 0 | 7 | 16 |
| `AboutDialog` | GUI.Forms-only | 5 | 0 | 0 | 1 | 6 |
| `FolderBrowserDialog` | `FolderBrowserDialog` | 6 | 0 | 0 | 15 | 3 |
| `ColorDialog` | `ColorDialog` | 5 | 0 | 0 | 9 | 2 |
| `ToolTip` | `ToolTip` | 11 | 0 | 0 | 24 | 8 |
| `RibbonToolTip` | GUI.Forms-only | 15 | 0 | 0 | 0 | 15 |
| `DrawingSurface` | GUI.Forms-only | 2 | 0 | 0 | 0 | 2 |
| `RasterCanvas` | GUI.Forms-only | 8 | 0 | 0 | 1 | 9 |
| `LiveSurface` | GUI.Forms-only | 10 | 0 | 0 | 0 | 10 |
| `SubSurfaceHost` | GUI.Forms-only | 14 | 0 | 0 | 1 | 15 |
| `MetricsView` | GUI.Forms-only | 1 | 0 | 0 | 0 | 1 |
| `Timer` | `Timer` | 6 | 0 | 0 | 3 | 1 |
| `BackgroundWorker` | `BackgroundWorker` | 9 | 0 | 0 | 1 | 10 |
| `NotifyIcon` | `NotifyIcon` | 12 | 0 | 0 | 14 | 6 |
| `OpenFileDialog` | `OpenFileDialog` | 11 | 0 | 0 | 6 | 4 |
| `SaveFileDialog` | `SaveFileDialog` | 9 | 0 | 0 | 5 | 4 |
| `ImageList` | `ImageList` | 16 | 0 | 0 | 14 | 19 |
| `ErrorProvider` | `ErrorProvider` | 14 | 0 | 0 | 17 | 9 |
| `HelpProvider` | `HelpProvider` | 16 | 0 | 0 | 7 | 10 |
| `HelpBook` | GUI.Forms-only | 9 | 0 | 0 | 17 | 26 |
| `HelpBookViewer` | GUI.Forms-only | 26 | 0 | 0 | 16 | 42 |
| `HelpBookSystem` | `Help` | 30 | 0 | 0 | 13 | 40 |
| `SemanticFeedback` | GUI.Forms-only | 7 | 0 | 0 | 1 | 8 |
| `Binding` | `Binding` | 8 | 0 | 0 | 22 | 7 |
| `BindingSource` | `BindingSource` | 45 | 0 | 0 | 60 | 28 |
| `BindingContext` | `BindingContext` | 8 | 0 | 0 | 7 | 2 |
| `TextBoxBase` | `TextBoxBase` | 1 | 0 | 0 | 46 | 0 |
| `ListControl` | `ListControl` | 0 | 0 | 0 | 34 | 0 |
| `UpDownBase` | `UpDownBase` | 1 | 0 | 0 | 30 | 1 |
| `Window` | `Form` | 215 | 10 | 6 | 140 | 231 |

## Windows Forms types without a GUI.Forms type

| Windows Forms | Type status | Note | Supported | Deferred | Excluded | Unclassified | GUI.Forms-only |
|---|---|---|---|---|---|---|---|
| `ColumnHeader` | supported | ListViewColumn records, not a control | 0 | 0 | 0 | 14 | 0 |
| `DataGridViewTextBoxColumn` | supported | DataGridViewColumnKind | 0 | 0 | 0 | 3 | 0 |
| `DataGridViewCheckBoxColumn` | supported | DataGridViewColumnKind | 0 | 0 | 0 | 6 | 0 |
| `DataGridViewComboBoxColumn` | supported | DataGridViewColumnKind | 0 | 0 | 0 | 12 | 0 |
| `DataGridViewButtonColumn` | supported | DataGridViewColumnKind | 0 | 0 | 0 | 4 | 0 |
| `DataGridViewImageColumn` | supported | DataGridViewColumnKind::image | 0 | 0 | 0 | 6 | 0 |
| `DataGridViewLinkColumn` | supported | DataGridViewColumnKind::link; LinkBehavior not supported | 0 | 0 | 0 | 8 | 0 |
| `DataGridViewTextBoxEditingControl` | unclassified | w-partial: the grid's own editors, not public types | 0 | 0 | 0 | 11 | 0 |
| `DataGridViewComboBoxEditingControl` | unclassified | w-partial: the grid's own editors, not public types | 0 | 0 | 0 | 11 | 0 |
| `ToolStripDropDown` | unclassified | w-partial: no public drop-down that hosts arbitrary content | 0 | 0 | 0 | 56 | 0 |
| `ToolStripDropDownMenu` | unclassified | w-partial: no public drop-down that hosts arbitrary content | 0 | 0 | 0 | 4 | 0 |
| `ToolStripContentPanel` | supported | ToolStripContainer::content_panel(), a Panel | 0 | 0 | 0 | 20 | 0 |
| `ToolStripPanelRow` | unclassified | w-partial: not a public type; several strips share a row | 0 | 0 | 0 | 17 | 0 |
| `ToolStripItem` | supported | item records (ToolStripItemKind) | 0 | 0 | 0 | 159 | 0 |
| `ToolStripDropDownItem` | supported | item records (ToolStripItemKind) | 0 | 0 | 0 | 19 | 0 |
| `ToolStripButton` | supported | item kinds | 0 | 0 | 0 | 9 | 0 |
| `ToolStripLabel` | supported | item kinds | 0 | 0 | 0 | 7 | 0 |
| `ToolStripSeparator` | supported | item kinds | 0 | 0 | 0 | 16 | 0 |
| `ToolStripDropDownButton` | supported | item kinds | 0 | 0 | 0 | 3 | 0 |
| `ToolStripControlHost` | unclassified | w-partial: the hosted kinds are fixed; an arbitrary hosted control is not supported | 0 | 0 | 0 | 36 | 0 |
| `ToolStripComboBox` | supported | hosted item kinds | 0 | 0 | 0 | 39 | 0 |
| `ToolStripTextBox` | supported | hosted item kinds | 0 | 0 | 0 | 53 | 0 |
| `ToolStripProgressBar` | supported | hosted item kinds | 0 | 0 | 0 | 21 | 0 |
| `ToolStripStatusLabel` | supported | StatusStrip labels | 0 | 0 | 0 | 6 | 0 |
| `ToolStripMenuItem` | supported | menu items | 0 | 0 | 0 | 13 | 0 |
| `ToolStripOverflow` | supported | the strip's overflow menu | 0 | 0 | 0 | 1 | 0 |
| `CommonDialog` | supported | no public base by design; the dialogs carry the members | 0 | 2 | 3 | 4 | 0 |
| `FileDialog` | supported | no public base by design; OpenFileDialog and SaveFileDialog carry the members | 0 | 0 | 7 | 16 | 0 |
| `MessageBox` | supported | show_message_box (the host's message dialog) | 0 | 0 | 0 | 1 | 0 |
| `AxHost` | excluded | Stage W excludes it: ActiveX hosting. | 0 | 0 | 78 | 0 | 0 |
| `WebBrowserBase` | excluded | Stage W excludes it: browser hosting. | 0 | 0 | 45 | 0 | 0 |
| `WebBrowser` | excluded | Stage W excludes it: browser hosting. | 0 | 0 | 59 | 0 | 0 |
| `MdiClient` | excluded | Stage W excludes it: MDI (stage W's exclusion list; MDI was since admitted as a presentation of DK's document area, not as MdiClient). | 0 | 0 | 3 | 0 | 0 |
| `PrintPreviewControl` | excluded | Stage W excludes it: printing. | 0 | 0 | 13 | 0 | 0 |
| `PrintPreviewDialog` | excluded | Stage W excludes it: printing. | 0 | 0 | 66 | 0 | 0 |
| `PrintDialog` | excluded | Stage W excludes it: printing. | 0 | 0 | 11 | 0 | 0 |
| `PageSetupDialog` | excluded | Stage W excludes it: printing. | 0 | 0 | 12 | 0 | 0 |
| `ComponentEditorPage` | excluded | Stage W excludes it: design time. | 0 | 0 | 0 | 0 | 0 |
| `NativeWindow` | unclassified | w-review: a platform-extension review row under ADR-001 | 0 | 0 | 0 | 13 | 0 |

## Unclassified members, by question

| Question | Members |
|---|---|
| `carried` | 104 |
| `hook` | 417 |
| `no-entry` | 517 |
| `review` | 13 |
| `unmatched` | 1629 |
| `untested` | 803 |
| `w-open` | 5 |
