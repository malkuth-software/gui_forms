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
| `Name` | `StableId`, required and immutable | Unique within a window, checked at attach time. It names the control in metrics, the semantic tree, automation and logs. A stable ID is one or more `.`-separated segments of lowercase ASCII letters and digits joined by single `-`: `textBox1` is spelled `text-box1`, and a refused ID's error states the rule. `name()` is a separate mutable property. |
| Properties (`button.Text = "OK"`) | Property cells: `(*button).text = "OK";`, read with `(*button).text.read()` | Writing an equal value does nothing. Every actual change marks dirtiness and emits a typed change event. There are no getter and setter pairs for properties. See [Properties](properties). |
| `Click += handler` with `(sender, EventArgs)` | `on(button.clicked(), owner, &Owner::method)` | The owner is a `Component` that keeps the connection; disposing it disconnects the handler, so you never need `-=`. A handler accepts the leading part of the event's typed arguments, or none. |
| `MouseDown`, `MouseUp`, `MouseMove`, `MouseWheel`, `MouseClick`, `MouseDoubleClick` with `MouseEventArgs` | `mouse_down()`, `mouse_up()`, `mouse_move()`, `mouse_wheel()`, `mouse_click()`, `mouse_double_click()` with `MouseEventArgs` | Same order as in Forms. `MouseEventArgs` has `button`, `clicks`, `location` (control coordinates), `window_position`, `wheel_delta` and `modifiers`. The host supplies `clicks`. See [Mouse events and context requests](mouse-events). |
| `Click`, `DoubleClick` (on any control) | `click()`, `double_click()` | Raised on the release over the pressed control, as in Forms. `DoubleClick` comes on the second release. A button's `clicked()` is its activation, raised after `Click` and also by `perform_click()` and the keyboard. |
| `MouseEnter`, `MouseHover`, `MouseLeave`, `MouseCaptureChanged` | `mouse_enter()`, `mouse_hover()`, `mouse_leave()`, `mouse_capture_changed()` | `MouseHover` is raised once per entry. On Windows it comes from `WM_MOUSEHOVER`; elsewhere it comes from the window's timer. |
| `SetStyle(ControlStyles.StandardClick / StandardDoubleClick)`, `GetStyle` | `set_style(ControlStyles::standard_click / standard_double_click, on)`, `has_style` | Same meaning. A button clears `standard_double_click` and clicks for the primary button only. |
| `ContextMenuStrip` property, `WM_CONTEXTMENU`, Shift+F10 | `context_menu_requested()` with `ContextMenuRequest`, or override `on_context_menu_requested` | Show your `ContextMenu` from the handler and set `handled`. Otherwise the request goes on to each ancestor. Each request has a pointer or keyboard origin. The Linux host emulates both kinds. |
| `ProcessCmdKey` overrides for window-wide keys | A window rule: `window.rules()`, `trigger::key_pressed` | Rules run before the control route. A consuming rule ends the key. See [Rules](rules). |
| `BeginInit` / `EndInit` | `begin_init()` / `end_init()` | During initialization each change event holds only its latest value and is published when the outermost `end_init()` runs. |
| `SuspendLayout` / `ResumeLayout` / `PerformLayout` | `suspend_layout()` / `resume_layout(perform)` / `perform_layout()` | Per control, as in Forms. `Window::begin_update()` adds a window-wide `UpdateScope` that batches every kind of change, not only layout. See [Update scopes and dirtiness](update-scopes-and-dirtiness). |
| `Invalidate()`, `Invalidate(Rectangle)`, `Refresh()`, `Update()` | `invalidate(Dirty)`, `invalidate(Rect)` | Invalidation says *what* changed: measure, arrange, paint, hit test, text, semantics. Invalidation never paints; the host paints the accumulated damage on its next frame. |
| `OnPaint(PaintEventArgs)` and `Graphics` | Override `on_paint(Painter&, Rect local_damage)`, or use `DrawingSurface` | `Painter` is a renderer-neutral drawing vocabulary over a CPU rasterizer. See [Painting and damage](painting-and-damage). |
| `Handle`, `CreateControl`, `WndProc` | None | Controls are not native windows. Only the top-level window is native, and reparenting a control creates nothing native. |
| `Form` and `Application.Run(form)` | The window class a `.form` document generates (a `Window` subclass), `Application::create(config)` and `application.run(std::make_unique<Form1>(model), options)`, which owns the window for the run | `Window` is a complete model that also runs without a native window, which is how tests run. The application compiles the program's theme once and every window it makes joins it. `ApplicationWindowOptions` carries the title, sizes and the `ready`, `closing` and `closed` callbacks. Up to 64 windows run in one host. |
| `FormClosing` with `e.Cancel` | `closing(HostCloseRequest&)` with `cancel` | The request carries its `reason`. |
| `AcceptButton`, `CancelButton`, `DialogResult` | `accept-button` and `cancel-button` on the form (`Window::set_accept_button`, `set_cancel_button` in code), `Button::set_dialog_result` | Return and Escape activate the targets when the focused control does not use the key. A button copies its result to the window after its click handlers run. Assigning a cancel button gives it `DialogResult::cancel` if it had none. |
| `ShowDialog()` | No modal run loop for your own windows | The window's `dialog_result()` is retained state and closes nothing; act on `dialog_result_changed()`. Use an owned dialog panel (`TaskDialog`, shown as an overlay or in a Popup window), an overlay (`AnchoredOverlayLayer`, `Window::open_overlay`) with a focus scope, or a second window. See [Overlays, Popups and Splash](overlays-popups-and-splash). |
| `MessageBox`, `OpenFileDialog`, `SaveFileDialog`, `FolderBrowserDialog`, `ColorDialog` | Typed requests to `Window::host_services()->show_dialog(...)` | Each request returns a typed result. A host that cannot show a dialog reports `unsupported`. |
| `InvokeRequired`, `Invoke`, `BeginInvoke` | `invoke_required()`, `invoke`, `begin_invoke` | `begin_invoke` returns a `DispatchOperation` with explicit `cancel()`. A property write or other mutation called from the wrong thread on an attached control throws. There is no `DoEvents`. See [Threading](threading). |
| `BackgroundWorker` | `Worker` and `CancellationFlag`, then `begin_invoke` | The worker never touches a control; it posts results to the UI thread. |
| `System.Windows.Forms.Timer` | `Timer(Window&)` with `tick()` | Bound to one window and fires on its UI thread. |
| `Dock`, `Anchor` | `dock = DockStyle::fill`, `anchor = AnchorStyles::top` (properties: assign to write, `.read()` to read) | Same values. The default anchor is top and left. Dock places children backmost first. A fill child takes the whole remaining rectangle, so a later docked sibling gets a zero slot; Windows Forms overlaps it instead. This divergence is deliberate. |
| Integer pixels, `AutoScaleMode` | Logical pixels as `double`; the window's `scale` | `Rect{x, y, width, height}` is in the parent's client space. `set_bounds` takes `BoundsSpecified` with the Forms flag values. See [Layout](layout). |
| `DataBindings.Add("Text", source, "Name")` | `data_bindings().add("text", source, "name", options)` | `BindingSource` holds `BindingRecord` rows with named fields, not reflected objects. `Value<T>` holds scalar application state shared between controls. |
| `CausesValidation`, `Validating`, `Validated`, `AutoValidate` | `causes_validation`, `validating()`, `validated()`, `AutoValidate` | Under the default `AutoValidate` policy, cancelling `validating` keeps focus where it is. See [Input and focus](input-and-focus). |
| `TabIndex`, `TabStop`, `Focus()` | `tab_index`, `tab_stop`, `Window::request_focus(control)` | Mnemonics use the same `&File` convention; `&&` shows one ampersand. |
| `Tag` | `set_tag(std::any)` | The value is released when the control is disposed. |
| `ToolTip.SetToolTip`, `ErrorProvider.SetError` | `(*control).tool_tip_text = text` or `ToolTip::set_tool_tip(control, text)`; `ErrorProvider::set_error(control, text)` | Tooltip text belongs to the control, and `ToolTip` writes it there. Each window shows tooltips through one presenter, with the timing every `ToolTip` of the window shares; `Window::set_tool_tips_shown` and `Application::set_tool_tips_shown` turn them off. A text box, or another control you type into, shows its tooltip as grey hint text in its empty field when it has focus; a placeholder takes precedence. Providers are components bound to one window. |
| `ListViewItem.ToolTipText`, `ListView.ShowItemToolTips`, `TreeNode.ToolTipText`, `TreeView.ShowNodeToolTips`, `TabControl.ShowToolTips` | None | GUI.Forms does not provide these members. Data items, such as list rows and tree nodes, have no tooltips. When an item's text is cut off, `ListView` (in the details and list views) and `TreeView` show the whole text in place while the pointer is over it. A tab header shows its `TabPage`'s own tooltip text, and empty text shows nothing. |
| `AccessibleName`, `AccessibleDescription` | `accessible_name`, `accessible_description` | Optional. Every stock control already publishes a role, name and state. |
| System fonts, visual styles, `SystemColors` | Bundled fonts and a `Theme` set on the window | GUI.Forms draws every control itself and never uses host fonts for control text. |
| Designer, `InitializeComponent`, `.resx` | A `.form` document, generated at build time by `gui_forms_add_form` | The document is authoritative and round-trips byte for byte; the generated window class is disposable. Colours and fonts go in a theme package. See [Forms](forms) and [Converting an existing application](converting-an-app). |

### Example: accept and cancel buttons

```text
/* forms/confirm.form */
form confirm.window {
  text: "Confirm";
  client-size: 400 140;
  accept-button: confirm.ok;
  cancel-button: confirm.cancel;   /* assigns DialogResult::cancel */

  panel confirm.root {
    button confirm.ok { text: "OK"; bounds: 200 96 88 32; anchor: bottom right; }
    button confirm.cancel { text: "Cancel"; bounds: 296 96 88 32; anchor: bottom right; }
  }
}
```

```cpp
// gui_forms_add_form(my_app forms/confirm.form CLASS ConfirmWindow NAMESPACE my_app DERIVED)
class Confirm final : public gf::Component {
public:
    explicit Confirm(my_app::ConfirmWindow& window) {
        (*window.confirm_ok).set_dialog_result(gf::DialogResult::ok);
        gf::on(window.dialog_result_changed(), *this, &Confirm::on_result);
    }

    gf::DialogResult last_result{gf::DialogResult::none};

private:
    // Nothing closes the window for you; act on the result here.
    void on_result(gf::DialogResult result) { last_result = result; }
};

my_app::ConfirmWindow window{};   // binds nothing, so it takes no model
Confirm confirm(window);          // declared after the window, so it ends first
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
| Dialogs and overlays | [`AnchoredOverlayLayer`](../api/generated/gui_forms.AnchoredOverlayLayer.rst) |
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
