# Member manifest

Generated from `manifests/members.json` by `python3 tools/control_registry.py render`. Do not edit.

Every GUI.Forms control and every property, event and method on each, against its Windows Forms (.NET 8 or later) counterpart: supported, deferred or excluded, or unclassified until someone decides. Each control's API reference page lists its members; this page counts them. `python3 tools/control_registry.py members --check` fails when the manifest and the registry disagree.

## Totals

| Supported | Deferred | Excluded | Unclassified | GUI.Forms-only |
|---|---|---|---|---|
| 2083 | 13 | 367 | 4116 | 2188 |

## GUI.Forms types

| Type | Windows Forms | Supported | Deferred | Excluded | Unclassified | GUI.Forms-only |
|---|---|---|---|---|---|---|
| `Control` | `Control` | 231 | 0 | 12 | 312 | 198 |
| `Component` | `Component` | 9 | 0 | 0 | 11 | 11 |
| `ButtonBase` | `ButtonBase` | 18 | 0 | 0 | 36 | 21 |
| `Button` | `Button` | 6 | 0 | 0 | 6 | 5 |
| `DropDownButton` | `ToolStripSplitButton` | 6 | 0 | 0 | 18 | 5 |
| `LinkLabel` | `LinkLabel` | 1 | 0 | 0 | 17 | 0 |
| `CommandBar` | `ToolStrip` | 5 | 0 | 2 | 71 | 3 |
| `TextBox` | `TextBox` | 23 | 0 | 0 | 28 | 31 |
| `TokenBox` | GUI.Forms-only | 12 | 0 | 0 | 2 | 14 |
| `SearchField` | GUI.Forms-only | 6 | 0 | 0 | 6 | 12 |
| `MaskedTextBox` | `MaskedTextBox` | 16 | 0 | 0 | 38 | 7 |
| `RichTextBox` | `RichTextBox` | 26 | 0 | 2 | 55 | 20 |
| `Label` | `Label` | 7 | 0 | 1 | 36 | 9 |
| `TextBlock` | GUI.Forms-only | 3 | 0 | 0 | 7 | 10 |
| `Badge` | GUI.Forms-only | 3 | 0 | 0 | 4 | 7 |
| `InfoBanner` | GUI.Forms-only | 3 | 0 | 0 | 5 | 8 |
| `EmptyState` | GUI.Forms-only | 2 | 0 | 0 | 3 | 5 |
| `Toast` | GUI.Forms-only | 4 | 0 | 0 | 1 | 5 |
| `PictureBox` | `PictureBox` | 6 | 0 | 2 | 33 | 7 |
| `ProgressBar` | `ProgressBar` | 8 | 0 | 1 | 34 | 11 |
| `ProgressRing` | GUI.Forms-only | 5 | 0 | 0 | 3 | 8 |
| `Spinner` | GUI.Forms-only | 2 | 0 | 0 | 2 | 4 |
| `SweepScope` | GUI.Forms-only | 2 | 0 | 0 | 3 | 5 |
| `Sparkline` | GUI.Forms-only | 6 | 0 | 0 | 4 | 10 |
| `PlotView` | GUI.Forms-only | 18 | 0 | 0 | 11 | 29 |
| `PlotAxisItem` | GUI.Forms-only | 2 | 0 | 0 | 8 | 10 |
| `PlotSeriesItem` | GUI.Forms-only | 8 | 0 | 0 | 19 | 27 |
| `PlotAnnotationItem` | GUI.Forms-only | 0 | 0 | 0 | 4 | 4 |
| `ListBox` | `ListBox` | 14 | 0 | 1 | 48 | 14 |
| `CheckedListBox` | `CheckedListBox` | 12 | 0 | 0 | 18 | 6 |
| `ComboBox` | `ComboBox` | 11 | 0 | 1 | 54 | 9 |
| `TreeView` | `TreeView` | 10 | 0 | 2 | 75 | 9 |
| `ObjectView` | `ListView` | 20 | 0 | 2 | 107 | 18 |
| `ListView` | `ListView` | 49 | 0 | 2 | 98 | 38 |
| `TreeListView` | GUI.Forms-only | 31 | 0 | 0 | 5 | 36 |
| `VirtualRowsView` | GUI.Forms-only | 33 | 0 | 0 | 2 | 35 |
| `DataGridView` | `DataGridView` | 130 | 0 | 0 | 330 | 79 |
| `ChoiceGroup` | GUI.Forms-only | 6 | 0 | 0 | 3 | 9 |
| `BreadcrumbTrail` | GUI.Forms-only | 12 | 0 | 0 | 3 | 15 |
| `CheckBox` | `CheckBox` | 4 | 0 | 0 | 14 | 3 |
| `RadioButton` | `RadioButton` | 4 | 0 | 0 | 11 | 3 |
| `ToggleSwitch` | GUI.Forms-only | 4 | 0 | 0 | 3 | 7 |
| `RangeControl` | GUI.Forms-only | 8 | 0 | 0 | 6 | 14 |
| `TrackBar` | `TrackBar` | 12 | 0 | 1 | 29 | 8 |
| `ValueBar` | GUI.Forms-only | 18 | 0 | 0 | 19 | 37 |
| `NumericUpDown` | `NumericUpDown` | 7 | 0 | 0 | 16 | 6 |
| `DomainUpDown` | `DomainUpDown` | 3 | 0 | 0 | 13 | 5 |
| `ColorValueEditor` | GUI.Forms-only | 3 | 0 | 0 | 3 | 6 |
| `FlagsValueEditor` | GUI.Forms-only | 1 | 0 | 0 | 6 | 7 |
| `PropertyGrid` | `PropertyGrid` | 19 | 0 | 2 | 72 | 19 |
| `PropertyList` | GUI.Forms-only | 16 | 0 | 0 | 5 | 21 |
| `DateTimePicker` | `DateTimePicker` | 6 | 0 | 1 | 46 | 4 |
| `MonthCalendar` | `MonthCalendar` | 18 | 0 | 1 | 44 | 5 |
| `Panel` | `Panel` | 8 | 0 | 0 | 8 | 7 |
| `GroupBox` | `GroupBox` | 4 | 0 | 0 | 21 | 1 |
| `ScaledGroupBox` | GUI.Forms-only | 3 | 0 | 0 | 2 | 5 |
| `MaterialPanel` | GUI.Forms-only | 1 | 0 | 0 | 0 | 1 |
| `Card` | GUI.Forms-only | 10 | 0 | 0 | 1 | 11 |
| `ExpandableSections` | GUI.Forms-only | 5 | 0 | 0 | 0 | 5 |
| `ContainerControl` | `ContainerControl` | 9 | 0 | 0 | 11 | 4 |
| `UserControl` | `UserControl` | 5 | 0 | 0 | 7 | 4 |
| `TabControl` | `TabControl` | 16 | 1 | 2 | 38 | 12 |
| `PageStrip` | GUI.Forms-only | 30 | 0 | 0 | 7 | 37 |
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
| `ScrollableControl` | `ScrollableControl` | 16 | 0 | 0 | 20 | 17 |
| `ScrollBar` | `ScrollBar` | 12 | 0 | 0 | 24 | 6 |
| `HScrollBar` | `HScrollBar` | 1 | 0 | 0 | 0 | 0 |
| `VScrollBar` | `VScrollBar` | 1 | 0 | 0 | 1 | 0 |
| `MenuStrip` | `MenuStrip` | 9 | 0 | 0 | 9 | 7 |
| `ToolStrip` | `ToolStrip` | 15 | 0 | 2 | 76 | 18 |
| `RibbonFontControl` | GUI.Forms-only | 9 | 0 | 0 | 1 | 10 |
| `RibbonColorPicker` | GUI.Forms-only | 7 | 0 | 0 | 2 | 9 |
| `BackstageView` | GUI.Forms-only | 3 | 0 | 0 | 2 | 5 |
| `RibbonGallery` | GUI.Forms-only | 18 | 0 | 0 | 7 | 25 |
| `StatusStrip` | `StatusStrip` | 1 | 0 | 1 | 9 | 0 |
| `QuickAccessToolbar` | GUI.Forms-only | 10 | 0 | 0 | 0 | 10 |
| `ToolStripPanel` | `ToolStripPanel` | 8 | 0 | 1 | 17 | 5 |
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
| `NodeCanvas` | GUI.Forms-only | 48 | 0 | 0 | 16 | 64 |
| `LiveSurface` | GUI.Forms-only | 10 | 0 | 0 | 0 | 10 |
| `SubSurfaceHost` | GUI.Forms-only | 14 | 0 | 0 | 1 | 15 |
| `MetricsView` | GUI.Forms-only | 1 | 0 | 0 | 0 | 1 |
| `Timer` | `Timer` | 6 | 0 | 0 | 3 | 1 |
| `BackgroundWorker` | `BackgroundWorker` | 10 | 0 | 0 | 6 | 2 |
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
| `TextBoxBase` | `TextBoxBase` | 11 | 0 | 0 | 40 | 4 |
| `ListControl` | `ListControl` | 2 | 0 | 0 | 33 | 1 |
| `UpDownBase` | `UpDownBase` | 3 | 0 | 5 | 23 | 1 |
| `Window` | `Form` | 220 | 10 | 10 | 132 | 232 |

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
| `ListViewItem` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 32 | 0 |
| `ListViewGroup` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 14 | 0 |
| `TreeNode` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 47 | 0 |
| `DataGridViewBand` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 15 | 0 |
| `DataGridViewRow` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 23 | 0 |
| `DataGridViewColumn` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 21 | 0 |
| `DataGridViewCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 95 | 0 |
| `DataGridViewCellStyle` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 19 | 0 |
| `DataGridViewTextBoxCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 2 | 0 |
| `DataGridViewCheckBoxCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 10 | 0 |
| `DataGridViewComboBoxCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 12 | 0 |
| `DataGridViewButtonCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 3 | 0 |
| `DataGridViewImageCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 4 | 0 |
| `DataGridViewLinkCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 8 | 0 |
| `DataGridViewHeaderCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 2 | 0 |
| `DataGridViewColumnHeaderCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 2 | 0 |
| `DataGridViewRowHeaderCell` | unclassified | nested: an item, cell or band type the controls hold; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `AxHost.AboutBoxDelegate` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 1 | 0 | 0 |
| `AxHost.ActiveXInvokeKind` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 0 | 0 | 0 |
| `AxHost.AxComponentEditor` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 1 | 0 | 0 |
| `AxHost.ClsidAttribute` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 2 | 0 | 0 |
| `AxHost.ConnectionPointCookie` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 2 | 0 | 0 |
| `AxHost.InvalidActiveXStateException` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 1 | 0 | 0 |
| `AxHost.State` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 2 | 0 | 0 |
| `AxHost.StateConverter` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 1 | 0 | 0 |
| `AxHost.TypeLibraryTimeStampAttribute` | excluded | Stage W excludes it: nested in AxHost: ActiveX hosting. | 0 | 0 | 2 | 0 | 0 |
| `ButtonBase.ButtonBaseAccessibleObject` | unclassified | nested: nested in ButtonBase; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `CheckBox.CheckBoxAccessibleObject` | unclassified | nested: nested in CheckBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `CheckedListBox.CheckedIndexCollection` | unclassified | nested: nested in CheckedListBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 7 | 0 |
| `CheckedListBox.CheckedItemCollection` | unclassified | nested: nested in CheckedListBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 7 | 0 |
| `CheckedListBox.ObjectCollection` | unclassified | nested: nested in CheckedListBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 2 | 0 |
| `ComboBox.ChildAccessibleObject` | unclassified | nested: nested in ComboBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `ComboBox.ObjectCollection` | unclassified | nested: nested in ComboBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 14 | 0 |
| `Control.ControlAccessibleObject` | unclassified | nested: nested in Control; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 4 | 0 |
| `Control.ControlCollection` | unclassified | nested: nested in Control; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 16 | 0 |
| `DataGridView.DataGridViewAccessibleObject` | unclassified | nested: nested in DataGridView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DataGridView.DataGridViewControlCollection` | unclassified | nested: nested in DataGridView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 3 | 0 |
| `DataGridView.DataGridViewTopRowAccessibleObject` | unclassified | nested: nested in DataGridView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 2 | 0 |
| `DataGridView.HitTestInfo` | unclassified | nested: nested in DataGridView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 6 | 0 |
| `DataGridViewButtonCell.DataGridViewButtonCellAccessibleObject` | unclassified | nested: nested in DataGridViewButtonCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DataGridViewCell.DataGridViewCellAccessibleObject` | unclassified | nested: nested in DataGridViewCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 2 | 0 |
| `DataGridViewCheckBoxCell.DataGridViewCheckBoxCellAccessibleObject` | unclassified | nested: nested in DataGridViewCheckBoxCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DataGridViewColumnHeaderCell.DataGridViewColumnHeaderCellAccessibleObject` | unclassified | nested: nested in DataGridViewColumnHeaderCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DataGridViewComboBoxCell.DataGridViewComboBoxCellAccessibleObject` | unclassified | nested: nested in DataGridViewComboBoxCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DataGridViewComboBoxCell.ObjectCollection` | unclassified | nested: nested in DataGridViewComboBoxCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 14 | 0 |
| `DataGridViewImageCell.DataGridViewImageCellAccessibleObject` | unclassified | nested: nested in DataGridViewImageCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DataGridViewLinkCell.DataGridViewLinkCellAccessibleObject` | unclassified | nested: nested in DataGridViewLinkCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DataGridViewRow.DataGridViewRowAccessibleObject` | unclassified | nested: nested in DataGridViewRow; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 2 | 0 |
| `DataGridViewRowHeaderCell.DataGridViewRowHeaderCellAccessibleObject` | unclassified | nested: nested in DataGridViewRowHeaderCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DataGridViewTextBoxCell.DataGridViewTextBoxCellAccessibleObject` | unclassified | nested: nested in DataGridViewTextBoxCell; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DateTimePicker.DateTimePickerAccessibleObject` | unclassified | nested: nested in DateTimePicker; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `DomainUpDown.DomainItemAccessibleObject` | unclassified | nested: nested in DomainUpDown; no GUI.Forms type stands for it yet | 0 | 0 | 1 | 0 | 0 |
| `DomainUpDown.DomainUpDownAccessibleObject` | unclassified | nested: nested in DomainUpDown; no GUI.Forms type stands for it yet | 0 | 0 | 1 | 0 | 0 |
| `DomainUpDown.DomainUpDownItemCollection` | unclassified | nested: nested in DomainUpDown; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 0 | 0 |
| `Form.ControlCollection` | unclassified | nested: nested in Form; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `ImageList.ImageCollection` | unclassified | nested: nested in ImageList; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 18 | 0 |
| `LinkLabel.Link` | unclassified | nested: nested in LinkLabel; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 9 | 0 |
| `LinkLabel.LinkCollection` | unclassified | nested: nested in LinkLabel; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 15 | 0 |
| `ListBox.IntegerCollection` | unclassified | nested: nested in ListBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 11 | 0 |
| `ListBox.ObjectCollection` | unclassified | nested: nested in ListBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 14 | 0 |
| `ListBox.SelectedIndexCollection` | unclassified | nested: nested in ListBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 11 | 0 |
| `ListBox.SelectedObjectCollection` | unclassified | nested: nested in ListBox; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 11 | 0 |
| `ListView.CheckedIndexCollection` | unclassified | nested: nested in ListView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 7 | 0 |
| `ListView.CheckedListViewItemCollection` | unclassified | nested: nested in ListView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 10 | 0 |
| `ListView.ColumnHeaderCollection` | unclassified | nested: nested in ListView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 16 | 0 |
| `ListView.ListViewItemCollection` | unclassified | nested: nested in ListView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 18 | 0 |
| `ListView.SelectedIndexCollection` | unclassified | nested: nested in ListView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 11 | 0 |
| `ListView.SelectedListViewItemCollection` | unclassified | nested: nested in ListView; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 11 | 0 |
| `ListViewItem.ListViewSubItem` | unclassified | nested: nested in ListViewItem; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 9 | 0 |
| `ListViewItem.ListViewSubItemCollection` | unclassified | nested: nested in ListViewItem; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 16 | 0 |
| `MdiClient.ControlCollection` | excluded | Stage W excludes it: nested in MdiClient: MDI (stage W's exclusion list; MDI was since admitted as a presentation of DK's document area, not as MdiClient). | 0 | 0 | 1 | 0 | 0 |
| `MonthCalendar.HitArea` | unclassified | nested: nested in MonthCalendar; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 0 | 0 |
| `MonthCalendar.HitTestInfo` | unclassified | nested: nested in MonthCalendar; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 3 | 0 |
| `PropertyGrid.PropertyTabCollection` | unclassified | nested: nested in PropertyGrid; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 6 | 0 |
| `RadioButton.RadioButtonAccessibleObject` | unclassified | nested: nested in RadioButton; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `ScrollableControl.DockPaddingEdges` | unclassified | nested: nested in ScrollableControl; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 5 | 0 |
| `ScrollableControl.DockPaddingEdgesConverter` | unclassified | nested: nested in ScrollableControl; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `TabControl.ControlCollection` | unclassified | nested: nested in TabControl; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `TabControl.TabPageCollection` | unclassified | nested: nested in TabControl; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 16 | 0 |
| `TabPage.TabPageControlCollection` | unclassified | nested: nested in TabPage; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `ToolStrip.ToolStripAccessibleObject` | unclassified | nested: nested in ToolStrip; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `ToolStripControlHost.ToolStripHostedControlAccessibleObject` | unclassified | nested: nested in ToolStripControlHost; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `ToolStripDropDown.ToolStripDropDownAccessibleObject` | unclassified | nested: nested in ToolStripDropDown; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `ToolStripItem.ToolStripItemAccessibleObject` | unclassified | nested: nested in ToolStripItem; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 2 | 0 |
| `ToolStripPanel.ToolStripPanelRowCollection` | unclassified | nested: nested in ToolStripPanel; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 11 | 0 |
| `ToolStripSplitButton.ToolStripSplitButtonAccessibleObject` | unclassified | nested: nested in ToolStripSplitButton; no GUI.Forms type stands for it yet | 0 | 0 | 0 | 1 | 0 |
| `WebBrowser.WebBrowserSite` | excluded | Stage W excludes it: nested in WebBrowser: browser hosting. | 0 | 0 | 1 | 0 | 0 |

## Unclassified members, by question

| Question | Members |
|---|---|
| `carried` | 116 |
| `hook` | 304 |
| `nested` | 641 |
| `no-entry` | 497 |
| `review` | 13 |
| `unmatched` | 1633 |
| `untested` | 907 |
| `w-open` | 5 |
