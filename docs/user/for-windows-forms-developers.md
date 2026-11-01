# GUI.Forms for Windows Forms developers

GUI.Forms keeps the vocabulary of `System.Windows.Forms`: the same control
families, Dock and Anchor, Tab order, mnemonics, validation, accept and cancel
buttons, `Invoke`, data bindings and extender providers. What changes is the
substrate. It is a C++20 library with explicit ownership, a retained tree that
owns no native handles, typed invalidation and one UI thread per window. This
page lists the differences that matter when you port Forms habits, then maps
every Forms type to its GUI.Forms counterpart.

## Key differences

| In Windows Forms | In GUI.Forms | Notes |
|---|---|---|
| Garbage collection and `Dispose()` | `std::shared_ptr` ownership; `Component::dispose()` | A parent owns its children strongly and a child refers to its parent weakly. `dispose()` is synchronous and idempotent. See [Retained tree](retained-tree). |
| `new Button()`, then `Controls.Add` | `make_control<Button>(StableId("ok"), "OK")`, then `add_child` | `remove_child(RuntimeId)` returns the detached child; a `shared_ptr` you hold keeps it alive. |
| `Name` | `StableId`, required and immutable | Unique within a window, checked at attach time. It names the control in metrics, the semantic tree, automation and logs. `name()` is a separate mutable property. |
| Properties (`button.Text = "OK"`) | Accessor pairs (`text()`, `set_text("OK")`) | Writing an equal value does nothing. Every actual change marks dirtiness and emits a typed change event. |
| `Click += handler` with `(sender, EventArgs)` | `on(button.clicked(), owner, &Owner::method)` | The owner is a `Component` that keeps the connection; disposing it disconnects the handler, so you never need `-=`. A handler accepts the leading part of the event's typed arguments, or none. |
| `BeginInit` / `EndInit` | `begin_init()` / `end_init()` | During initialization each change event holds only its latest value and is published when the outermost `end_init()` runs. |
| `SuspendLayout` / `ResumeLayout` / `PerformLayout` | `suspend_layout()` / `resume_layout(perform)` / `perform_layout()` | Per control, as in Forms. `Window::begin_update()` adds a window-wide `UpdateScope` that batches every kind of change, not only layout. See [Update scopes and dirtiness](update-scopes-and-dirtiness). |
| `Invalidate()`, `Invalidate(Rectangle)`, `Refresh()`, `Update()` | `invalidate(Dirty)`, `invalidate(Rect)` | Invalidation says *what* changed: measure, arrange, paint, hit test, text, semantics. Invalidation never paints; the host paints the accumulated damage on its next frame. |
| `OnPaint(PaintEventArgs)` and `Graphics` | Override `on_paint(Painter&, Rect local_damage)`, or use `DrawingSurface` | `Painter` is a renderer-neutral drawing vocabulary over a CPU rasterizer. See [Painting and damage](painting-and-damage). |
| `Handle`, `CreateControl`, `WndProc` | None | Controls are not native windows. Only the top-level window is native, and reparenting a control creates nothing native. |
| `Form` and `Application.Run(form)` | `Window` and `Application::run(window, options)` | `Window` is a complete model that also runs without a native window, which is how tests run. `ApplicationWindowOptions` carries the title, sizes and the `ready`, `closing` and `closed` callbacks. Up to 64 windows run in one host. |
| `FormClosing` with `e.Cancel` | `closing(HostCloseRequest&)` with `cancel` | The request carries its `reason`. |
| `AcceptButton`, `CancelButton`, `DialogResult` | `Window::set_accept_button`, `set_cancel_button`, `Button::set_dialog_result` | Return and Escape activate the targets when the focused control does not use the key. A button copies its result to the window after its click handlers run. Assigning a cancel button gives it `DialogResult::cancel` if it had none. |
| `ShowDialog()` | No modal run loop for your own windows | The window's `dialog_result()` is retained state and closes nothing; act on `dialog_result_changed()`. Use an owned popup (`AnchoredPopupLayer`, `Window::open_popup`) with a focus scope, or a second window. |
| `MessageBox`, `OpenFileDialog`, `SaveFileDialog`, `FolderBrowserDialog`, `ColorDialog` | Typed requests to `Window::host_services()->show_dialog(...)` | Each request returns a typed result. A host that cannot show a dialog reports `unsupported`. |
| `InvokeRequired`, `Invoke`, `BeginInvoke` | `invoke_required()`, `invoke`, `begin_invoke` | `begin_invoke` returns a `DispatchOperation` with explicit `cancel()`. A setter called from the wrong thread on an attached control throws. There is no `DoEvents`. See [Threading](threading). |
| `BackgroundWorker` | `Worker` and `CancellationFlag`, then `begin_invoke` | The worker never touches a control; it posts results to the UI thread. |
| `System.Windows.Forms.Timer` | `Timer(Window&)` with `tick()` | Bound to one window and fires on its UI thread. |
| `Dock`, `Anchor` | `set_dock(DockStyle)`, `set_anchor(AnchorStyles)` | Same values. The default anchor is top and left. Dock places children backmost first. |
| Integer pixels, `AutoScaleMode` | Logical pixels as `double`; the window's `scale` | `Rect{x, y, width, height}` is in the parent's client space. `set_bounds` takes `BoundsSpecified` with the Forms flag values. See [Layout](layout). |
| `DataBindings.Add("Text", source, "Name")` | `data_bindings().add("text", source, "name", options)` | `BindingSource` holds `BindingRecord` rows with named fields, not reflected objects. `Value<T>` holds scalar application state shared between controls. |
| `CausesValidation`, `Validating`, `Validated`, `AutoValidate` | `causes_validation`, `validating()`, `validated()`, `AutoValidate` | Under the default `AutoValidate` policy, cancelling `validating` keeps focus where it is. See [Input and focus](input-and-focus). |
| `TabIndex`, `TabStop`, `Focus()` | `set_tab_index`, `set_tab_stop`, `Window::request_focus(control)` | Mnemonics use the same `&File` convention; `&&` shows one ampersand. |
| `Tag` | `set_tag(std::any)` | The value is released when the control is disposed. |
| `ToolTip.SetToolTip`, `ErrorProvider.SetError` | `Control::set_tool_tip_text(text)` or `ToolTip::set_tool_tip(control, text)`; `ErrorProvider::set_error(control, text)` | Tooltip text belongs to the control, and `ToolTip` writes it there. Each window shows tooltips through one presenter, with the timing every `ToolTip` of the window shares; `Window::set_tool_tips_shown` and `Application::set_tool_tips_shown` turn them off. A text box, or another control you type into, shows its tooltip as grey hint text in its empty field when it has focus; a placeholder takes precedence. Providers are components bound to one window. |
| `ListViewItem.ToolTipText`, `ListView.ShowItemToolTips`, `TreeNode.ToolTipText`, `TreeView.ShowNodeToolTips`, `TabControl.ShowToolTips` | None | GUI.Forms does not provide these members. Data items, such as list rows and tree nodes, have no tooltips. When an item's text is cut off, `ListView` (in the details and list views) and `TreeView` show the whole text in place while the pointer is over it. A tab header shows its `TabPage`'s own tooltip text, and empty text shows nothing. |
| `AccessibleName`, `AccessibleDescription` | `set_accessible_name`, `set_accessible_description` | Optional. Every stock control already publishes a role, name and state. |
| System fonts, visual styles, `SystemColors` | Bundled fonts and a `Theme` set on the window | GUI.Forms draws every control itself and never uses host fonts for control text. |
| Designer, `InitializeComponent`, `.resx` | Code | Trees are built in C++. |

### Example: accept and cancel buttons

```cpp
class Confirm final : public gf::Component {
public:
    Confirm() {
        root = gf::make_control<gf::Panel>(gf::StableId("confirm"));
        ok = gf::make_control<gf::Button>(gf::StableId("confirm.ok"), "OK");
        cancel = gf::make_control<gf::Button>(gf::StableId("confirm.cancel"), "Cancel");
        (*ok).set_bounds({200.0, 96.0, 88.0, 32.0});
        (*cancel).set_bounds({296.0, 96.0, 88.0, 32.0});
        (*ok).set_anchor(gf::AnchorStyles::bottom | gf::AnchorStyles::right);
        (*cancel).set_anchor(gf::AnchorStyles::bottom | gf::AnchorStyles::right);
        (*ok).set_dialog_result(gf::DialogResult::ok);
        (*root).add_child(ok);
        (*root).add_child(cancel);
    }

    // Accept and cancel targets must already belong to the window.
    void attach(gf::Window& window) {
        window.set_accept_button(ok);
        window.set_cancel_button(cancel);  // assigns DialogResult::cancel
        gf::on(window.dialog_result_changed(), *this, &Confirm::on_result);
    }

    std::shared_ptr<gf::Panel> root{};
    std::shared_ptr<gf::Button> ok{};
    std::shared_ptr<gf::Button> cancel{};
    gf::DialogResult last_result{gf::DialogResult::none};

private:
    // Nothing closes the window for you; act on the result here.
    void on_result(gf::DialogResult result) { last_result = result; }
};

Confirm confirm{};
gf::Window window(confirm.root, gf::Size{400.0, 140.0});
confirm.attach(window);
```

## Type correspondence

Status comes from the control registry; see
[status labels](controls.md#status-labels). Rows marked *core* are not
controls but play the role of the Forms type.

| Windows Forms | GUI.Forms | Status | Notes |
|---|---|---|---|
| `Application` | [`Application`](../api/generated/gui_forms.Application.rst) | core | Runs one window or a set of windows on the native host. |
| `BackgroundWorker` | [`Worker`](../api/generated/gui_forms.Worker.rst) | core | With [`CancellationFlag`](../api/generated/gui_forms.CancellationFlag.rst). |
| `Binding` | [`Binding`](../api/generated/gui_forms.Binding.rst) | stable | |
| `BindingContext` | [`BindingContext`](../api/generated/gui_forms.BindingContext.rst) | stable | |
| `BindingSource` | [`BindingSource`](../api/generated/gui_forms.BindingSource.rst) | stable | Holds `BindingRecord` rows. |
| `Button` | [`Button`](../api/generated/gui_forms.Button.rst) | stable | |
| `ButtonBase` | [`ButtonBase`](../api/generated/gui_forms.ButtonBase.rst) | stable | |
| `CheckBox` | [`CheckBox`](../api/generated/gui_forms.CheckBox.rst) | stable | |
| `CheckedListBox` | [`CheckedListBox`](../api/generated/gui_forms.CheckedListBox.rst) | stable | |
| `ComboBox` | [`ComboBox`](../api/generated/gui_forms.ComboBox.rst) | stable | The `DropDownList` and `DropDown` styles (not `Simple`); auto-complete from the items or a custom list. |
| `Component` | [`Component`](../api/generated/gui_forms.Component.rst) | core | |
| `ContainerControl` | [`ContainerControl`](../api/generated/gui_forms.ContainerControl.rst) | stable | |
| `ContextMenuStrip` | [`ContextMenu`](../api/generated/gui_forms.ContextMenu.rst) | stable | Rows are bound to [`Command`](../api/generated/gui_forms.Command.rst) objects. |
| `Control` | [`Control`](../api/generated/gui_forms.Control.rst) | core | |
| `DateTimePicker` | [`DateTimePicker`](../api/generated/gui_forms.DateTimePicker.rst) | stable | Calendar or spin presentation. |
| `ErrorProvider` | [`ErrorProvider`](../api/generated/gui_forms.ErrorProvider.rst) | stable | |
| `FlowLayoutPanel` | [`FlowLayoutPanelCompat`](../api/generated/gui_forms.FlowLayoutPanelCompat.rst) | stable | Imported by its Windows Forms name; use `FlexPanel` for new work |
| `Form` | [`Window`](../api/generated/gui_forms.Window.rst) | core | Presented by `Application::run`. |
| `GroupBox` | [`GroupBox`](../api/generated/gui_forms.GroupBox.rst) | stable | |
| `HelpProvider` | [`HelpProvider`](../api/generated/gui_forms.HelpProvider.rst) | stable | |
| `HScrollBar` | [`HScrollBar`](../api/generated/gui_forms.HScrollBar.rst) | stable | |
| `ImageList` | [`ImageList`](../api/generated/gui_forms.ImageList.rst) | stable | Bound to one window. |
| `Label` | [`Label`](../api/generated/gui_forms.Label.rst) | stable | |
| `LinkLabel` | [`LinkLabel`](../api/generated/gui_forms.LinkLabel.rst) | stable | |
| `ListBox` | [`ListBox`](../api/generated/gui_forms.ListBox.rst) | stable | |
| `ListView` | [`ObjectView`](../api/generated/gui_forms.ObjectView.rst) | stable | Icon and details views over stable item IDs. |
| `MenuStrip` | [`MenuStrip`](../api/generated/gui_forms.MenuStrip.rst) | stable | Drawn inside the window, not published to a native menu bar. |
| `NumericUpDown` | [`NumericUpDown`](../api/generated/gui_forms.NumericUpDown.rst) | stable | Decimal or hexadecimal. |
| `Panel` | [`Panel`](../api/generated/gui_forms.Panel.rst) | stable | |
| `PictureBox` | [`PictureBox`](../api/generated/gui_forms.PictureBox.rst) | stable | All five size modes. |
| `ProgressBar` | [`ProgressBar`](../api/generated/gui_forms.ProgressBar.rst) | stable | |
| `PropertyGrid` | [`PropertyGrid`](../api/generated/gui_forms.PropertyGrid.rst) | stable | Edits the property descriptors of `Control` objects. |
| `RadioButton` | [`RadioButton`](../api/generated/gui_forms.RadioButton.rst) | stable | |
| `ScrollableControl` | [`ScrollableControl`](../api/generated/gui_forms.ScrollableControl.rst) | stable | |
| `ScrollBar` | [`ScrollBar`](../api/generated/gui_forms.ScrollBar.rst) | stable | |
| `SplitContainer` | [`SplitContainer`](../api/generated/gui_forms.SplitContainer.rst) | stable | |
| `SplitterPanel` | [`SplitterPanel`](../api/generated/gui_forms.SplitterPanel.rst) | stable | |
| `TabControl` | [`TabControl`](../api/generated/gui_forms.TabControl.rst) | stable | |
| `TabPage` | [`TabPage`](../api/generated/gui_forms.TabPage.rst) | stable | |
| `TableLayoutPanel` | [`TableLayoutPanelCompat`](../api/generated/gui_forms.TableLayoutPanelCompat.rst) | stable | Imported by its Windows Forms name; use `GridPanel` for new work |
| `TextBox` | [`TextBox`](../api/generated/gui_forms.TextBox.rst) | stable | Single-line or multiline; password character; auto-complete from a custom list (`AutoCompleteMode`, `AutoCompleteCustomSource`). |
| `Timer` | [`Timer`](../api/generated/gui_forms.Timer.rst) | stable | |
| `ToolStrip` | [`CommandBar`](../api/generated/gui_forms.CommandBar.rst) | provisional | A row of command buttons built from a replaceable item list. |
| `ToolStripSplitButton` | [`DropDownButton`](../api/generated/gui_forms.DropDownButton.rst) | provisional | Whole-button or split drop-down. |
| `ToolTip` | [`ToolTip`](../api/generated/gui_forms.ToolTip.rst) | stable | Sets the controls' own tooltip text and the window's timing. |
| `TrackBar` | [`TrackBar`](../api/generated/gui_forms.TrackBar.rst) | stable | |
| `TreeView` | [`TreeView`](../api/generated/gui_forms.TreeView.rst) | stable | |
| `UserControl` | [`UserControl`](../api/generated/gui_forms.UserControl.rst) | stable | `loaded()` fires once, on first attachment. |
| `VScrollBar` | [`VScrollBar`](../api/generated/gui_forms.VScrollBar.rst) | stable | |

### Types with no Forms counterpart

| Function | GUI.Forms types |
|---|---|
| Information display | [`ReviewCard`](../api/generated/gui_forms.ReviewCard.rst) |
| Selection from a list | [`CorrespondenceView`](../api/generated/gui_forms.CorrespondenceView.rst), [`ChoiceGroup`](../api/generated/gui_forms.ChoiceGroup.rst) (provisional), [`BreadcrumbTrail`](../api/generated/gui_forms.BreadcrumbTrail.rst) (provisional) |
| Value setting | [`RangeControl`](../api/generated/gui_forms.RangeControl.rst), [`ColorValueEditor`](../api/generated/gui_forms.ColorValueEditor.rst), [`FlagsValueEditor`](../api/generated/gui_forms.FlagsValueEditor.rst), [`PropertyList`](../api/generated/gui_forms.PropertyList.rst), [`InstrumentRack`](../api/generated/gui_forms.InstrumentRack.rst) |
| Grouping | [`Card`](../api/generated/gui_forms.Card.rst), [`MaterialPanel`](../api/generated/gui_forms.MaterialPanel.rst), [`ScaledGroupBox`](../api/generated/gui_forms.ScaledGroupBox.rst), [`ExpandableSections`](../api/generated/gui_forms.ExpandableSections.rst) (provisional) |
| Layout | [`MasterDetailView`](../api/generated/gui_forms.MasterDetailView.rst), [`ScaledPanel`](../api/generated/gui_forms.ScaledPanel.rst), [`ResponsiveTrackPanel`](../api/generated/gui_forms.ResponsiveTrackPanel.rst) (provisional), [`CommandOverflowPanel`](../api/generated/gui_forms.CommandOverflowPanel.rst) (provisional) |
| Menus and commands | [`Command`](../api/generated/gui_forms.Command.rst) |
| Dialogs and popups | [`AnchoredPopupLayer`](../api/generated/gui_forms.AnchoredPopupLayer.rst) |
| Drawing | [`DrawingSurface`](../api/generated/gui_forms.DrawingSurface.rst), [`RasterCanvas`](../api/generated/gui_forms.RasterCanvas.rst), [`LiveSurface`](../api/generated/gui_forms.LiveSurface.rst) |
| Components | [`SemanticFeedback`](../api/generated/gui_forms.SemanticFeedback.rst) |
| Diagnostics (lab) | [`MetricsView`](../api/generated/gui_forms.MetricsView.rst), [`VisualInspectorView`](../api/generated/gui_forms.VisualInspectorView.rst), [`VisualInspectorOverlay`](../api/generated/gui_forms.VisualInspectorOverlay.rst), [`TypographySpecimen`](../api/generated/gui_forms.TypographySpecimen.rst), [`EasingPreview`](../api/generated/gui_forms.EasingPreview.rst) |

## Not provided

These are outside the library by design, not missing work:

- MDI (`MdiClient` and MDI parent and child forms).
- Printing: `PrintDialog`, `PageSetupDialog`, `PrintPreviewControl`,
  `PrintPreviewDialog` and the printing pipeline.
- ActiveX and COM hosting (`AxHost`) and browser hosting (`WebBrowser`).
- The designer and its serialization, and design-time services.
- Obsolete families: the classic `DataGrid`, `MainMenu`, `MenuItem`, the
  classic `ContextMenu`, `StatusBar`, `ToolBar` and the legacy `Splitter`.
  Use `MenuStrip`, `ContextMenu` (for `ContextMenuStrip`) and
  `SplitContainer`.
- Per-control native handles and window-message processing.
- GPU rendering of any kind, host font enumeration for control text, and
  image codecs other than PNG.
- Nonvisual designer components that are not user interface, such as event
  logs, file watchers, serial ports and processes.

## Not yet available

The host auto-complete sources (file system, history, URLs) report
unsupported on every host. Outbound drag and native menu-bar publication
are not available on any host. The [roadmap](../dev/roadmap)
tracks what is in progress.
