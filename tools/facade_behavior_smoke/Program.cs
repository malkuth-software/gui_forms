using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Threading;
using System.Windows.Forms;

if (args.Length == 1 && args[0] == "timer")
{
    return RunTimerHost();
}
if (args.Length == 1 && args[0] == "doevents")
{
    return RunDoEventsHost();
}
if (args.Length == 1 && args[0] == "lifecycle")
{
    return RunLoadLifecycle();
}
if (args.Length == 1 && args[0] == "lifecycle-order")
{
    return RunInitializationOrder();
}
if (args.Length == 1 && args[0] == "pointer")
{
    return RunPointerHost();
}
if (args.Length == 1 && args[0] == "button")
{
    return RunRasterButtonHost();
}
if (args.Length == 1 && args[0] == "dialog")
{
    return RunDialogHost();
}
if (args.Length == 1 && args[0] == "menu")
{
    return RunMenuHost();
}
if (args.Length == 1 && args[0] == "menu-live")
{
    return RunOverlayMenuHost();
}
if (args.Length == 1 && args[0] == "menu-hover")
{
    return RunHoverMenuHost();
}
if (args.Length == 1 && args[0] == "popup-repeat")
{
    return RunPopupRepeatHost();
}
if (args.Length == 1 && args[0] == "combo")
{
    return RunComboHost();
}
if (args.Length == 1 && args[0] == "field-live")
{
    return RunFieldLiveHost();
}
if (args.Length == 1 && args[0] == "font-thread")
{
    return RunCrossThreadFontHost();
}
if (args.Length == 1 && args[0] == "thread-mutation")
{
    return RunThreadMutationHost();
}
if (args.Length == 1 && args[0] == "secondary-form")
{
    return RunSecondaryFormHost();
}
if (args.Length == 1 && args[0] == "theme-inherited-paint")
{
    return RunInheritedThemePaintHost();
}
if (args.Length == 1 && args[0] == "background-only-paint")
{
    return RunBackgroundOnlyPaintHost();
}
if (args.Length == 1 && args[0] == "theme-restore-transaction")
{
    return RunThemeRestoreTransactionHost();
}
if (args.Length == 1 && args[0] == "form-semantics")
{
    return RunFormSemantics();
}
if (args.Length == 1 && args[0] == "cursor")
{
    return RunCursorSemantics();
}
if (args.Length == 1 && args[0] == "dock-padding")
{
    return RunDockPaddingSemantics();
}
if (args.Length == 1 && args[0] == "initial-dock-show")
{
    return RunInitialDockShowSemantics();
}
if (args.Length == 1 && args[0] == "control-geometry")
{
    return RunControlGeometrySemantics();
}
if (args.Length == 1 && args[0] == "layout-transactions")
{
    return RunLayoutTransactionSemantics();
}
if (args.Length == 1 && args[0] == "split-container")
{
    return RunSplitContainerSemantics();
}
if (args.Length == 1 && args[0] == "dialog-key-live")
{
    return RunDialogKeyLiveHost();
}
if (args.Length == 1 && args[0] == "scroll-panel")
{
    return RunScrollablePanelHost();
}
if (args.Length == 1 && args[0] == "scroll-panel-live")
{
    return RunScrollablePanelLiveHost();
}
if (args.Length == 1 && args[0] == "numeric-edit")
{
    return RunNumericEditHost();
}
if (args.Length == 1 && args[0] == "numeric-topology")
{
    return RunNumericTopology();
}
if (args.Length == 1 && args[0] == "form-icon")
{
    return RunFormIconContract();
}
if (args.Length == 1 && args[0] == "constructor-callback-order")
{
    return RunConstructorCallbackOrder();
}
if (args.Length == 1 && args[0] == "text-edit")
{
    return RunTextEditHost();
}
if (args.Length == 1 && args[0] == "slider-live")
{
    return RunSliderLiveHost();
}
if (args.Length == 1 && args[0] == "messagebox")
{
    return RunMessageBoxHost();
}
if (args.Length == 1 && args[0] == "dialog-live")
{
    return RunNativeDialogLiveHost();
}
if (args.Length == 1 && args[0] == "folder-live")
{
    return RunNativeFolderDialogLiveHost();
}
if (args.Length == 1 && args[0] == "tooltip-live")
{
    return RunTooltipLiveHost();
}
if (args.Length == 1 && args[0] == "native-surface")
{
    Environment.SetEnvironmentVariable("GUI_FORMS_DIRECT_HWND_TYPES", "PaintInputProbe");
    return RunNativeWindowSurfaceHost();
}
if (args.Length == 1 && args[0] == "native-surface-lifecycle")
{
    Environment.SetEnvironmentVariable("GUI_FORMS_DIRECT_HWND_TYPES", "PaintInputProbe");
    return RunNativeWindowSurfaceLifecycleHost();
}
if (args.Length == 1 && args[0] == "native-surface-fallback")
{
    Environment.SetEnvironmentVariable("GUI_FORMS_DIRECT_HWND_TYPES", "PaintInputProbe");
    return RunNativeWindowSurfaceFallbackHost();
}
if (args.Length == 1 && args[0] == "paint-reentry")
{
    return RunPaintReentryHost();
}
if (args.Length == 1 && args[0] == "paint-input-deferral")
{
    return RunPaintInputDeferralHost();
}
if (args.Length == 1 && args[0] == "managed-double-buffer")
{
    return RunManagedDoubleBufferHost();
}
if (args.Length == 1 && args[0] == "managed-damage")
{
    return RunManagedDamageHost();
}
if (args.Length == 1 && args[0] == "visibility-paint")
{
    return RunVisibilityPaintHost();
}
if (args.Length == 1 && args[0] == "property-grid")
{
    return RunPropertyGridProjection();
}

var form = new Form { Name = "behaviorForm", Text = "M11d behavior", Size = new Size(640, 420) };
using var userControl = new UserControl();
using var pictureBox = new PictureBox();
using var pictureImage = new Bitmap(17, 9);
var table = new TableLayoutPanel
{
    Name = "settingsTable",
    Location = new Point(12, 18),
    Size = new Size(440, 260),
    ColumnCount = 2,
    RowCount = 3,
    Dock = DockStyle.Fill,
    Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
};
table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 40f));
table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 60f));
table.RowStyles.Add(new RowStyle(SizeType.AutoSize));

var enabled = new CheckBox { Name = "enabledCheck", Text = "Enabled", Checked = false };
var mode = new ComboBox { Name = "modeCombo", DropDownStyle = ComboBoxStyle.DropDownList };
var gain = new NumericUpDown { Name = "gainNumeric", Minimum = -20m, Maximum = 80m, Increment = 0.5m, DecimalPlaces = 1 };
var radio = new RadioButton { Name = "radioChoice" };
var stateProbe = new StateChangeProbe { Name = "stateProbe" };

var checkEvents = 0;
var selectionEvents = 0;
var valueEvents = 0;
var radioEvents = 0;
enabled.CheckedChanged += (_, _) => ++checkEvents;
mode.SelectedIndexChanged += (_, _) => ++selectionEvents;
gain.ValueChanged += (_, _) => ++valueEvents;
radio.CheckedChanged += (_, _) => ++radioEvents;

mode.Items.AddRange(["WFM", "NFM", "AM"]);
mode.SelectedIndex = 1;
gain.Value = 12.5m;
enabled.Checked = true;
radio.Checked = true;
stateProbe.Text = "ready";
stateProbe.Enabled = false;
stateProbe.Visible = false;
stateProbe.Visible = true;
stateProbe.ForeColor = Color.Navy;
stateProbe.BackColor = Color.White;
stateProbe.Font = new Font("Lucida Grande", 11f);

table.Controls.Add(enabled, 0, 0);
table.Controls.Add(mode, 1, 0);
table.Controls.Add(gain, 1, 1);
table.Controls.Add(radio, 0, 1);
table.SetColumnSpan(radio, 2);
form.Controls.Add(table);

var moveEvents = 0;
var sizeEvents = 0;
table.Move += (_, _) => ++moveEvents;
table.SizeChanged += (_, _) => ++sizeEvents;
table.Location = new Point(20, 24);
table.Size = new Size(460, 280);

var menu = new ToolStrip();
var renderer = new ToolStripProfessionalRenderer();
var startItem = new ToolStripMenuItem("Start") { Tag = "receiver", Size = new Size(80, 24) };
var modeItem = new ToolStripMenuItem("Mode");
var fmItem = new ToolStripMenuItem("FM");
menu.Items.Add(startItem);
modeItem.DropDownItems.Add(fmItem);

var padding = new Padding(1, 2, 3, 4);
var mouse = new MouseEventArgs(MouseButtons.Left, 2, 17, 23, 120);
var key = new KeyEventArgs(Keys.Control | Keys.A);
var layoutProbe = new LayoutProbe { Size = new Size(220, 90) };
var dockedChild = new Panel { Dock = DockStyle.Fill };
layoutProbe.Controls.Add(dockedChild);
var binding = new BindingSource { DataSource = new ArrayList { "notch-a", "notch-b" } };
var grid = new DataGridView { DataSource = binding };
var firstColumn = new DataGridViewTextBoxColumn();
var secondColumn = new DataGridViewTextBoxColumn();
grid.Columns.AddRange([firstColumn, secondColumn]);
var tableProbe = new TableLayoutPanel
{
    Size = new Size(200, 80), ColumnCount = 2, RowCount = 1, Padding = new Padding(0)
};
tableProbe.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 80f));
tableProbe.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
tableProbe.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
var tableLeft = new Button { Dock = DockStyle.Fill, Margin = new Padding(0) };
var tableRight = new Button { Dock = DockStyle.Fill, Margin = new Padding(0) };
tableProbe.Controls.Add(tableLeft, 0, 0);
tableProbe.Controls.Add(tableRight, 1, 0);
var tableTop = new Panel { Dock = DockStyle.Top, Height = 12, Margin = new Padding(0) };
tableProbe.SetColumnSpan(tableTop, 2);
tableProbe.Controls.Add(tableTop, 0, 0);
var boundedTable = new TableLayoutPanel
{
    Size = new Size(120, 40), ColumnCount = 3, RowCount = 1, Padding = new Padding(0)
};
boundedTable.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
boundedTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 40f));
boundedTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 60f));
boundedTable.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
var oversizedAuto = new Label { Size = new Size(300, 40), Margin = new Padding(0) };
var boundedMiddle = new Button { Dock = DockStyle.Fill, Margin = new Padding(0) };
var boundedRight = new Button { Dock = DockStyle.Fill, Margin = new Padding(0) };
boundedTable.Controls.Add(oversizedAuto, 0, 0);
boundedTable.Controls.Add(boundedMiddle, 1, 0);
boundedTable.Controls.Add(boundedRight, 2, 0);
var lateAutoSizeTable = new TableLayoutPanel
{
    Size = new Size(160, 40), ColumnCount = 2, RowCount = 1, Padding = new Padding(0)
};
lateAutoSizeTable.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
lateAutoSizeTable.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
lateAutoSizeTable.RowStyles.Add(new RowStyle(SizeType.AutoSize));
var lateAutoSizeLabel = new Label { AutoSize = true, Text = string.Empty, Size = Size.Empty, Margin = new Padding(0) };
pictureBox.Size = Size.Empty;
pictureBox.SizeMode = PictureBoxSizeMode.AutoSize;
lateAutoSizeTable.Controls.Add(lateAutoSizeLabel, 0, 0);
lateAutoSizeTable.Controls.Add(pictureBox, 1, 0);
lateAutoSizeLabel.Text = "Zoom";
pictureBox.Image = pictureImage;
var anchoredAutoSizeTable = new TableLayoutPanel
{
    Size = new Size(87, 26), ColumnCount = 1, RowCount = 1, Padding = new Padding(0),
};
anchoredAutoSizeTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
anchoredAutoSizeTable.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
var anchoredAutoSizeLabel = new Label
{
    AutoSize = true, Text = "Zoom", Size = new Size(59, 20), Margin = new Padding(3),
    Anchor = AnchorStyles.Left | AnchorStyles.Right, TextAlign = ContentAlignment.MiddleCenter,
};
anchoredAutoSizeTable.Controls.Add(anchoredAutoSizeLabel, 0, 0);
var autoPercentTable = new TableLayoutPanel
{
    Size = new Size(240, 0), ColumnCount = 1, RowCount = 2, Padding = new Padding(0),
    Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
};
autoPercentTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
autoPercentTable.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
autoPercentTable.RowStyles.Add(new RowStyle(SizeType.AutoSize));
var autoPercentPanel = new Panel { Size = new Size(240, 0), Dock = DockStyle.Top, AutoSize = true, Margin = new Padding(0) };
var autoPercentHost = new Panel { Size = new Size(240, 30), Dock = DockStyle.Top, Margin = new Padding(0) };
autoPercentPanel.Controls.Add(autoPercentHost);
var autoPercentFooter = new Label { Size = new Size(100, 20), Margin = new Padding(0) };
autoPercentTable.Controls.Add(autoPercentPanel, 0, 0);
autoPercentTable.Controls.Add(autoPercentFooter, 0, 1);
var autoPercentExpandedHeight = autoPercentTable.Height;
autoPercentPanel.Visible = false;
var autoPercentCollapsedHeight = autoPercentTable.Height;
var autoPercentCollapsedFooterTop = autoPercentFooter.Top;
autoPercentPanel.Visible = true;
var hiddenAutoRowTable = new TableLayoutPanel
{
    Size = new Size(180, 90), ColumnCount = 1, RowCount = 3, Padding = new Padding(0),
};
hiddenAutoRowTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
hiddenAutoRowTable.RowStyles.Add(new RowStyle(SizeType.AutoSize));
hiddenAutoRowTable.RowStyles.Add(new RowStyle(SizeType.AutoSize));
hiddenAutoRowTable.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
var hiddenAutoRow = new Label { Text = "not laid out", Size = new Size(180, 31), Margin = new Padding(0), Visible = false };
var visibleAutoRow = new Label { Text = "visible", Size = new Size(180, 19), Margin = new Padding(0) };
var remainingAutoRow = new Panel { Dock = DockStyle.Fill, Margin = new Padding(0) };
hiddenAutoRowTable.Controls.Add(hiddenAutoRow, 0, 0);
hiddenAutoRowTable.Controls.Add(visibleAutoRow, 0, 1);
hiddenAutoRowTable.Controls.Add(remainingAutoRow, 0, 2);
var flowProbe = new FlowLayoutPanel { Size = new Size(170, 80), Padding = new Padding(0), WrapContents = true };
var flowFirst = new Button { Size = new Size(80, 20), Margin = new Padding(0) };
var flowSecond = new Button { Size = new Size(80, 20), Margin = new Padding(0) };
var flowThird = new Button { Size = new Size(80, 20), Margin = new Padding(0) };
flowProbe.Controls.AddRange([flowFirst, flowSecond, flowThird]);
var listProbe = new ListBox { Size = new Size(160, 88) };
listProbe.Items.Add("alpha");
listProbe.Items.Add("gamma");
listProbe.Items.Insert(1, "beta");
listProbe.SelectedIndex = 2;
listProbe.Items.RemoveAt(2);
var splitProbe = new SplitContainer { Size = new Size(204, 80) };
splitProbe.PerformLayout();
var splitterProbe = new Splitter { SplitPosition = 37 };
var progressProbe = new ProgressBar();
progressProbe.Value = 42;
var progressRejected = false;
try { progressProbe.Value = 101; }
catch (ArgumentOutOfRangeException) { progressRejected = true; }
var projectedProgress = (double)(typeof(Control).GetProperty("__NativeRangeValue",
    global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!
    .GetValue(progressProbe) ?? -1d);
var tooltipProbe = new ToolTip();
tooltipProbe.SetToolTip(enabled, "Enable receiver");
var dialogFileVariable = Environment.GetEnvironmentVariable("GUI_FORMS_DIALOG_FILE");
var dialogFolderVariable = Environment.GetEnvironmentVariable("GUI_FORMS_DIALOG_FOLDER");
Environment.SetEnvironmentVariable("GUI_FORMS_DIALOG_FILE", "/tmp/gui-forms/radio.ini");
Environment.SetEnvironmentVariable("GUI_FORMS_DIALOG_FOLDER", "/tmp/gui-forms");
using var openDialogProbe = new OpenFileDialog { FileName = "old.ini", InitialDirectory = "/tmp" };
using var folderDialogProbe = new FolderBrowserDialog { SelectedPath = "/" };
var openDialogResult = openDialogProbe.ShowDialog();
var folderDialogResult = folderDialogProbe.ShowDialog();
Environment.SetEnvironmentVariable("GUI_FORMS_DIALOG_FILE", dialogFileVariable);
Environment.SetEnvironmentVariable("GUI_FORMS_DIALOG_FOLDER", dialogFolderVariable);
using var colorDialogProbe = new ColorDialog { Color = Color.CadetBlue };
var dateProbe = new DateTimePicker { CustomFormat = "yyyy-MM-dd", Value = new DateTime(2026, 8, 5) };
Clipboard.SetText("GUI.Forms retained clipboard");

Require(form.Size == new Size(640, 420), "form size");
Require(userControl.Size == new Size(150, 150), "user-control default size");
((System.ComponentModel.ISupportInitialize)gain).BeginInit();
((System.ComponentModel.ISupportInitialize)gain).EndInit();
((System.ComponentModel.ISupportInitialize)pictureBox).BeginInit();
((System.ComponentModel.ISupportInitialize)pictureBox).EndInit();
Require(table.Parent == form && enabled.Parent == table, "parenting");
Require(table.Controls.Count == 4 && table.Controls[1] == mode, "table collection");
Require(mode.Items.Count == 3 && Equals(mode.SelectedItem, "NFM"), "combo state");
Require(gain.Value == 12.5m && gain.Minimum == -20m && gain.Maximum == 80m, "numeric state");
Require(mode.Text == "NFM" && gain.Text == "12.5", "field text projection");
Require(gain.Controls.Count == 2 &&
        gain.Controls[0] is Button && gain.Controls[0].Name == "upDownButtons" &&
        gain.Controls[1] is TextBox && gain.Controls[1].Name == "upDownEdit",
    "numeric composite children");
using (var spin = new NumericProbe { Minimum = 0m, Maximum = 10m, Increment = 2m, Value = 5m, Size = new Size(120, 24) })
{
    spin.SpinAt(115, 4);
    Require(spin.Value == 7m && spin.Text == "7", "numeric upper spin");
    spin.SpinAt(115, 20);
    spin.WheelBy(120);
    Require(spin.Value == 7m, "numeric lower spin and wheel");
}
using (var textFont = new Font("Lucida Grande", 12f))
using (var textBitmap = new Bitmap(140, 30))
using (var textGraphics = Graphics.FromImage(textBitmap))
{
    var measured = TextRenderer.MeasureText("Radio", textFont);
    Require(measured.Width > 8 && measured.Height >= textFont.Height, "text renderer measurement");
    var narrowGlyphs = textGraphics.MeasureString("iiii", textFont);
    var wideGlyphs = textGraphics.MeasureString("WWWW", textFont);
    Require(wideGlyphs.Width > narrowGlyphs.Width,
        "drawing measurement uses glyph advances rather than character count");
    using var wrappedFormat = new StringFormat();
    var unboundedText = textGraphics.MeasureString("alpha beta gamma", textFont);
    var boundedText = textGraphics.MeasureString("alpha beta gamma", textFont,
        (int)(unboundedText.Width / 2f), wrappedFormat);
    Require(boundedText.Width <= unboundedText.Width / 2f &&
        boundedText.Height > unboundedText.Height, "drawing measurement wraps to layout width");
    Require(textGraphics.MeasureString(null!, textFont, 120, new StringFormat()) == SizeF.Empty,
        "drawing null text matches System.Drawing empty-span semantics");
    using var nullTextBrush = new SolidBrush(Color.Black);
    textGraphics.DrawString(null!, null!, nullTextBrush, PointF.Empty);
    textGraphics.DrawString(string.Empty, null!, nullTextBrush, PointF.Empty);
    textGraphics.Clear(Color.White);
    TextRenderer.DrawText(textGraphics, "Radio", textFont, new Rectangle(0, 0, 140, 30),
        Color.Black, TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
    var inkFound = false;
    for (var y = 0; y < textBitmap.Height && !inkFound; ++y)
        for (var x = 0; x < textBitmap.Width; ++x)
            if (textBitmap.GetPixel(x, y).ToArgb() != Color.White.ToArgb()) { inkFound = true; break; }
    Require(inkFound, "text renderer drawing");
    Require(textBitmap.GetPixel(textBitmap.Width - 1, textBitmap.Height - 1).ToArgb() ==
        Color.White.ToArgb(), "bitmap read flushes retained drawing before Graphics disposal");
    textGraphics.FillRectangle(nullTextBrush, textBitmap.Width - 1,
        textBitmap.Height - 1, 1, 1);
    Require(textBitmap.GetPixel(textBitmap.Width - 1, textBitmap.Height - 1).ToArgb() ==
        Color.Black.ToArgb(), "bitmap read incrementally commits later retained commands");
}
using (var sourceBitmap = new Bitmap(8, 8))
using (var sourceGraphics = Graphics.FromImage(sourceBitmap))
using (var targetBitmap = new Bitmap(8, 8))
using (var targetGraphics = Graphics.FromImage(targetBitmap))
{
    sourceGraphics.Clear(Color.Crimson);
    targetGraphics.DrawImageUnscaled(sourceBitmap, 0, 0);
    Require(targetBitmap.GetPixel(4, 4).ToArgb() == Color.Crimson.ToArgb(),
        "image draw flushes pending source drawing before sampling");
}
using (var transparentSource = new Bitmap(15, 15, System.Drawing.Imaging.PixelFormat.Format32bppPArgb))
using (var sourceGraphics = Graphics.FromImage(transparentSource))
using (var transparentTarget = new Bitmap(15, 15, System.Drawing.Imaging.PixelFormat.Format32bppPArgb))
using (var targetGraphics = Graphics.FromImage(transparentTarget))
using (var glyph = new SolidBrush(Color.White))
{
    sourceGraphics.Clear(Color.Transparent);
    sourceGraphics.FillRectangle(glyph, 6, 6, 3, 3);
    targetGraphics.Clear(Color.Transparent);
    targetGraphics.DrawImage(transparentSource, new Rectangle(0, 0, 15, 15));
    Require(transparentTarget.GetPixel(0, 0).A == 0 &&
        transparentTarget.GetPixel(7, 7).A == 255,
        "transparent image composition preserves owner-drawn glyph alpha");
}
using (var encodedGlyph = new System.IO.MemoryStream(Convert.FromBase64String(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4+PEjAwAIfgLUTwUv2QAAAABJRU5ErkJggg==")))
using (var decodedGlyph = Image.FromStream(encodedGlyph))
using (var transparentTarget = new Bitmap(1, 1, System.Drawing.Imaging.PixelFormat.Format32bppPArgb))
using (var targetGraphics = Graphics.FromImage(transparentTarget))
{
    targetGraphics.Clear(Color.Transparent);
    targetGraphics.DrawImage(decodedGlyph, new Rectangle(0, 0, 1, 1));
    Require(transparentTarget.GetPixel(0, 0).A == 0,
        "decoded transparent PNG pixels remain transparent when owner-drawn");
}
using (var encodedMask = new System.IO.MemoryStream(Convert.FromBase64String(
    "iVBORw0KGgoAAAANSUhEUgAAAA8AAAAPCAIAAAC0tAIdAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAAZdEVYdFNvZnR3YXJlAHBhaW50Lm5ldCA0LjAuMTCtCgrAAAAATUlEQVQoU9WOwQ0AIAgDGZlNGBkbS4ghEX3qvSi0gDyImbm7qoaeQKKJUeiVEuisJANnK2Hgygq4FaCI1o58gBe6QPm1CxQrOV/4AJEBcw9Zp4gf7LYAAAAASUVORK5CYII=")))
using (var decodedMask = new Bitmap(encodedMask))
{
    Require(decodedMask.GetPixel(0, 0).ToArgb() == Color.Black.ToArgb() &&
        decodedMask.GetPixel(7, 7).ToArgb() == Color.White.ToArgb() &&
        decodedMask.GetPixel(4, 4).R is > 0 and < 255,
        "decoded DockPanelSuite glyph mask preserves black, white, and anti-aliased samples");
    using var glyph = new Bitmap(decodedMask.Width, decodedMask.Height,
        System.Drawing.Imaging.PixelFormat.Format32bppArgb);
    var glyphData = glyph.LockBits(new Rectangle(0, 0, glyph.Width, glyph.Height),
        System.Drawing.Imaging.ImageLockMode.WriteOnly,
        System.Drawing.Imaging.PixelFormat.Format32bppArgb);
    try
    {
        for (var y = 0; y < glyph.Height; ++y)
        for (var x = 0; x < glyph.Width; ++x)
        {
            var offset = y * glyphData.Stride + x * 4;
            System.Runtime.InteropServices.Marshal.WriteByte(glyphData.Scan0, offset, 0xf1);
            System.Runtime.InteropServices.Marshal.WriteByte(glyphData.Scan0, offset + 1, 0xf1);
            System.Runtime.InteropServices.Marshal.WriteByte(glyphData.Scan0, offset + 2, 0xf1);
            System.Runtime.InteropServices.Marshal.WriteByte(glyphData.Scan0, offset + 3,
                decodedMask.GetPixel(x, y).B);
        }
    }
    finally { glyph.UnlockBits(glyphData); }
    using var dockButton = new Bitmap(glyph.Width, glyph.Height);
    using (var dockGraphics = Graphics.FromImage(dockButton))
    {
        dockGraphics.Clear(Color.FromArgb(42, 42, 42));
        dockGraphics.DrawImageUnscaled(glyph, 0, 0);
    }
    Require(dockButton.GetPixel(0, 0).R == 42 && dockButton.GetPixel(7, 7).R > 230,
        "straight-alpha LockBits glyph composites over a dark DockPanel caption");
}
using (var gradientBitmap = new Bitmap(1, 256))
using (var gradientGraphics = Graphics.FromImage(gradientBitmap))
using (var gradient = new System.Drawing.Drawing2D.LinearGradientBrush(
    new Rectangle(0, 0, 1, 255), Color.White, Color.Black,
    System.Drawing.Drawing2D.LinearGradientMode.Vertical))
using (var gradientPen = new Pen(gradient))
{
    gradientGraphics.DrawLine(gradientPen, 0, 0, 0, 255);
    Require(gradientBitmap.GetPixel(0, 0).GetBrightness() > 0.9f,
        "gradient-backed line preserves its first endpoint");
    Require(gradientBitmap.GetPixel(0, 255).GetBrightness() < 0.1f,
        "gradient-backed line preserves its inclusive far endpoint without wrapping");
}
using (var overlayBitmap = new Bitmap(220, 100))
using (var overlayGraphics = Graphics.FromImage(overlayBitmap))
using (var overlayFill = new SolidBrush(Color.FromArgb(200, 50, 50, 50)))
using (var overlayBorder = new Pen(Color.FromArgb(200, Color.Gray)))
using (var overlayFont = new Font("Helvetica", 13f))
using (var overlayPath = new System.Drawing.Drawing2D.GraphicsPath())
{
    var bounds = new RectangleF(18, 14, 174, 64);
    const float diameter = 12f;
    overlayPath.AddArc(new RectangleF(bounds.Left, bounds.Top, diameter, diameter), 180f, 90f);
    overlayPath.AddArc(new RectangleF(bounds.Right - diameter, bounds.Top, diameter, diameter), 270f, 90f);
    overlayPath.AddArc(new RectangleF(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter), 0f, 90f);
    overlayPath.AddArc(new RectangleF(bounds.Left, bounds.Bottom - diameter, diameter, diameter), 90f, 90f);
    overlayPath.CloseFigure();
    overlayGraphics.Clear(Color.Black);
    overlayGraphics.FillPath(overlayFill, overlayPath);
    overlayGraphics.DrawPath(overlayBorder, overlayPath);
    overlayGraphics.DrawString("99.935 MHz\r\n-91.2 dBFS", overlayFont, Brushes.White, 32f, 25f);
    overlayGraphics.Flush();
    Require(overlayBitmap.GetPixel(80, 40).ToArgb() != Color.Black.ToArgb(),
        "spectrum hover overlay rounded path and text render atomically");
}
using (var multilineBitmap = new Bitmap(180, 70))
using (var multilineGraphics = Graphics.FromImage(multilineBitmap))
using (var multilineFont = new Font("Helvetica", 13f))
{
    multilineGraphics.Clear(Color.Black);
    multilineGraphics.DrawString("99.935 MHz\r\n-91.2 dBFS", multilineFont,
        Brushes.White, 2f, 2f);
    multilineGraphics.Flush();
    var secondLineInk = false;
    for (var y = 26; y < multilineBitmap.Height && !secondLineInk; ++y)
        for (var x = 0; x < multilineBitmap.Width; ++x)
            if (multilineBitmap.GetPixel(x, y).GetBrightness() > 0.5f)
            {
                secondLineInk = true;
                break;
            }
    Require(secondLineInk, "DrawString honors CRLF as a second rendered line");

    using var alignedBitmap = new Bitmap(240, 80);
    using var alignedGraphics = Graphics.FromImage(alignedBitmap);
    alignedGraphics.Clear(Color.Black);
    using var alignedFont = new Font("Lucida Console", 10f);
    using var alignedBrush = new SolidBrush(Color.White);
    using var alignedFormat = new StringFormat
    {
        Alignment = StringAlignment.Center,
        LineAlignment = StringAlignment.Center,
    };
    alignedGraphics.DrawString("Band Plan", alignedFont, alignedBrush,
        new RectangleF(20f, 10f, 200f, 60f), alignedFormat);
    alignedGraphics.Flush();
    var alignedCenterInk = false;
    var alignedLeftInk = false;
    for (var y = 10; y < 70; ++y)
    {
        for (var x = 20; x < 220; ++x)
        {
            if (alignedBitmap.GetPixel(x, y).ToArgb() == Color.Black.ToArgb()) continue;
            if (x >= 80 && x <= 160 && y >= 28 && y <= 52) alignedCenterInk = true;
            if (x < 55) alignedLeftInk = true;
        }
    }
    Require(alignedCenterInk && !alignedLeftInk,
        "DrawString RectangleF honors centered StringFormat alignment");
}
using (var longLivedBitmap = new Bitmap(2, 2))
using (var longLivedGraphics = Graphics.FromImage(longLivedBitmap))
{
    Color last = default;
    for (var frame = 0; frame < 5000; ++frame)
    {
        last = Color.FromArgb(255, frame & 0xff, (frame >> 3) & 0xff,
            (frame >> 6) & 0xff);
        longLivedGraphics.Clear(last);
        longLivedGraphics.Flush();
    }
    var executedField = typeof(Graphics).GetField("__executedCommands",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
    Require(executedField is not null &&
        (ulong)executedField.GetValue(longLivedGraphics)! < 4096UL,
        "long-lived direct-surface graphics compacts executed command history");
    Require(longLivedBitmap.GetPixel(0, 0).ToArgb() == last.ToArgb(),
        "recorder compaction preserves the most recently committed frame");
}
Require(enabled.Checked && radio.Checked, "check state");
Require(checkEvents == 1 && selectionEvents == 1 && valueEvents == 1 && radioEvents == 1, "state events");
Require(stateProbe.TextChanges == 1 && stateProbe.EnabledChanges == 1 &&
    stateProbe.VisibleChanges == 2 && stateProbe.ForeColorChanges == 1 &&
    stateProbe.BackColorChanges == 1 && stateProbe.FontChanges == 1,
    "virtual state-change routing");
Require(moveEvents == 1 && sizeEvents >= 1 && table.ClientRectangle.Size == table.Size,
    "geometry events including retained host relayout");
Require(table.FindForm() == form && gain.PointToScreen(Point.Empty) ==
    new Point(table.Left + gain.Left, table.Top + gain.Top), "coordinate ancestry");
Require(menu.Items[0] == startItem && startItem.Owner == menu && startItem.Text == "Start" && Equals(startItem.Tag, "receiver"), "toolstrip state");
Require(ReferenceEquals(enabled.FlatAppearance, enabled.FlatAppearance), "flat appearance ownership");
Require(ReferenceEquals(renderer.ColorTable, renderer.ColorTable), "renderer color-table ownership");
Require(modeItem.DropDown.OwnerItem == modeItem && modeItem.DropDownItems.Count == 1 &&
    modeItem.DropDownItems[0] == fmItem, "dropdown item ownership");
Require(padding.Left == 1 && padding.Top == 2 && padding.Right == 3 && padding.Bottom == 4 && padding.All == -1, "padding state");
Require(mouse.Button == MouseButtons.Left && mouse.X == 17 && mouse.Y == 23 && mouse.Delta == 120, "mouse args");
Require(key.KeyCode == Keys.A && key.Modifiers == Keys.Control, "key args");
Require(gain.Focus(), "focus acceptance");
Require(gain.Focused, "focused state");
Require(form.ContainsFocus, "ancestor focus state");
Require(ReferenceEquals(form.ActiveControl, gain), "container active-control projection");
var dockPadding = new ScrollableControl.DockPaddingEdges { All = 4, Left = 7 };
Require(dockPadding.Left == 7 && dockPadding.Top == 4 && dockPadding.Right == 4 &&
    dockPadding.Bottom == 4, "dock padding edge state");
Require(Cursors.Default is not null && Cursors.Hand is not null && Cursors.HSplit is not null &&
    Cursors.VSplit is not null && ReferenceEquals(Cursors.Default, Cursors.Default),
    "cursor singleton identity");
Require(layoutProbe.Layouts > 0 && dockedChild.Bounds == layoutProbe.ClientRectangle,
    "virtual layout and fill docking");
Require(grid.Columns.Count == 2 && ReferenceEquals(grid.Columns[0], firstColumn) &&
    grid.Rows.Count == 2 && ReferenceEquals(grid.DefaultCellStyle, grid.DefaultCellStyle),
    "grid collections and binding source");
Require(binding.List.Count == 2 && Equals(binding.Current, "notch-a"), "binding source state");
Require(tableLeft.Bounds == new Rectangle(0, 0, 80, 80) &&
    tableRight.Bounds == new Rectangle(80, 0, 120, 80), "table layout cells");
Require(tableTop.Bounds == new Rectangle(0, 0, 200, 12), "table top docking");
Require(oversizedAuto.Right <= boundedTable.ClientSize.Width &&
    boundedMiddle.Right <= boundedTable.ClientSize.Width &&
    boundedRight.Right <= boundedTable.ClientSize.Width,
    "table layout containment");
Require(lateAutoSizeLabel.Width > 0 && lateAutoSizeLabel.Height > 0,
    "late text invalidates label auto-size layout");
Require(anchoredAutoSizeLabel.Bounds == new Rectangle(3, 3, 81, 20),
    $"parent table bounds remain authoritative during anchored AutoSize child relayout; actual={anchoredAutoSizeLabel.Bounds}");
Require(pictureBox.Width >= pictureImage.Width && pictureBox.Height >= pictureImage.Height,
    "picture image contributes auto-size preferred dimensions");
Require(autoPercentPanel.Height == 30 && autoPercentExpandedHeight == 50 &&
    autoPercentCollapsedHeight == 20 && autoPercentTable.Height == 50,
    $"auto-size table preserves percent-row content and reacts to visibility (panel={autoPercentPanel.Height}, collapsed-footer-top={autoPercentCollapsedFooterTop}, footer-top={autoPercentFooter.Top}, expanded={autoPercentExpandedHeight}, collapsed={autoPercentCollapsedHeight}, restored={autoPercentTable.Height})");
Require(visibleAutoRow.Top == 0 && visibleAutoRow.Height == 19 &&
    remainingAutoRow.Top == 19 && remainingAutoRow.Height == 71,
    "invisible controls do not reserve auto-sized table rows");
Require(flowFirst.Location == Point.Empty && flowSecond.Location == new Point(80, 0) &&
    flowThird.Location == new Point(0, 20), "flow layout wrapping");
Require(listProbe.Items.Count == 2 && Equals(listProbe.Items[0], "alpha") &&
    Equals(listProbe.Items[1], "beta") && listProbe.SelectedIndex == 1,
    "listbox collection and selection normalization");
Require(splitProbe.Panel1.Parent == splitProbe && splitProbe.Panel2.Parent == splitProbe &&
    splitProbe.Panel1.Right <= splitProbe.Panel2.Left && splitProbe.Panel2.Right <= splitProbe.Width,
    "split container panel ownership and containment");
Require(splitterProbe.SplitPosition == 37, "splitter position state");
Require(progressProbe.Minimum == 0 && progressProbe.Maximum == 100 && progressRejected,
    "progress range validation");
Require(projectedProgress == 42d, "progress native range projection");
Require(!progressProbe.Capture, "unattached capture query");
Require(tooltipProbe.GetToolTip(enabled) == "Enable receiver", "tooltip association state");
tooltipProbe.SetToolTip(enabled, string.Empty);
Require(tooltipProbe.GetToolTip(enabled) == string.Empty, "tooltip association removal");
Require(openDialogResult == DialogResult.OK && openDialogProbe.FileName == "/tmp/gui-forms/radio.ini" &&
    openDialogProbe.SafeFileName == "radio.ini", "file dialog provider state");
Require(folderDialogResult == DialogResult.OK && folderDialogProbe.SelectedPath == "/tmp/gui-forms",
    "folder dialog provider state");
Require(colorDialogProbe.Color == Color.CadetBlue, "color dialog state");
Require(dateProbe.Value == new DateTime(2026, 8, 5) && dateProbe.CustomFormat == "yyyy-MM-dd",
    "date-time state");
Require(Clipboard.GetText() == "GUI.Forms retained clipboard", "clipboard state");
Require(ControlPaint.Dark(Color.White).R < 255 && ControlPaint.Light(Color.Black).R > 0,
    "control-paint color transforms");
Require(Application.ExecutablePath.Length != 0 && Screen.AllScreens.Length == 1 &&
    Screen.GetWorkingArea(Point.Empty).Height > 0 && SystemInformation.DragSize.Width > 0,
    "environment compatibility state");

Console.WriteLine("surface=control,table,check,radio,combo,numeric,toolstrip,args");
Console.WriteLine($"events=check:{checkEvents}|selection:{selectionEvents}|value:{valueEvents}|radio:{radioEvents}|move:{moveEvents}|size:{sizeEvents}");
Console.WriteLine($"tree={form.Controls.Count}/{table.Controls.Count}|selected={mode.SelectedItem}|gain={gain.Value}|focus={gain.Focused}");

var disposalParent = new Panel();
var latePaintChild = new PaintInputProbe { Size = new Size(40, 20) };
disposalParent.Controls.Add(latePaintChild);
disposalParent.Dispose();
latePaintChild.Invalidate();

form.Dispose();
listProbe.Dispose();
splitProbe.Dispose();
splitterProbe.Dispose();
progressProbe.Dispose();
tooltipProbe.Dispose();
dateProbe.Dispose();
Console.WriteLine("behavior=pass");
return 0;

static int RunTimerHost()
{
    var form = new Form { Name = "timerForm", Text = "M11d timer", Size = new Size(360, 180) };
    using var timer = new System.Windows.Forms.Timer { Interval = 25 };
    var ticks = 0;
    timer.Tick += (_, _) =>
    {
        ++ticks;
        timer.Stop();
        form.Close();
    };
    timer.Start();
    Application.Run(form);
    Require(ticks == 1 && !timer.Enabled, "timer lifecycle");
    Console.WriteLine($"timer=ticks:{ticks}|enabled:{timer.Enabled}|host:{(Application.LastHostTrace.Contains("win32-dib", StringComparison.Ordinal) ? "win32-dib" : "other")}");
    form.Dispose();
    return 0;
}

static int RunVisibilityPaintHost()
{
    var form = new Form { Name = "visibilityPaintForm", Text = "Visibility paint", Size = new Size(360, 180) };
    var container = new Panel { Name = "visibilityContainer", Dock = DockStyle.Fill };
    var probe = new PaintInputProbe { Name = "visibilityPaintProbe", Size = new Size(120, 42) };
    container.Controls.Add(probe);
    form.Controls.Add(container);
    form.Load += (_, _) =>
    {
        var before = probe.Paints;
        container.Visible = false;
        container.Visible = true;
        Require(probe.Paints > before,
            "revealing an already-loaded retained subtree repaints custom controls");
        form.BeginInvoke((Action)form.Close);
    };
    Application.Run(form);
    Console.WriteLine($"visibility-paint=before-show:{probe.Paints - 1}|after-show:{probe.Paints}|subtree:repainted");
    form.Dispose();
    return 0;
}

static int RunDoEventsHost()
{
    var form = new Form { Name = "doEventsForm", Text = "Nested dispatch", Size = new Size(360, 180) };
    var nested = 0;
    var loadReturned = false;
    form.Load += (_, _) =>
    {
        form.BeginInvoke((Action)(() => ++nested));
        Application.DoEvents();
        Require(nested == 0, "BeginInvoke remains posted until the load callback unwinds");
        loadReturned = true;
        form.BeginInvoke((Action)form.Close);
    };
    Application.Run(form);
    Require(loadReturned && nested == 1, "posted dispatch runs exactly once after callback unwind");
    Console.WriteLine($"doevents=nested:{nested}|load-returned:{loadReturned}");
    form.Dispose();
    return 0;
}

static int RunThreadMutationHost()
{
    var form = new Form { Name = "threadMutationForm", Text = "Posted mutation", Size = new Size(360, 180) };
    var target = new Button { Name = "threadMutationTarget", Text = "Target", Enabled = true };
    form.Controls.Add(target);
    Thread? worker = null;
    form.Load += (_, _) =>
    {
        worker = new Thread(() =>
        {
            target.Enabled = false;
            form.BeginInvoke((Action)form.Close);
        });
        worker.Start();
    };
    Application.Run(form);
    worker?.Join();
    Require(!target.Enabled, "cross-thread compatibility mutation is posted before later BeginInvoke work");
    Console.WriteLine("thread-mutation=posted:true|ordered:true|enabled:false");
    form.Dispose();
    return 0;
}

static int RunSecondaryFormHost()
{
    var main = new Form { Name = "secondaryMain", Text = "Secondary owner", Size = new Size(640, 480) };
    var mainField = new TextBox { Name = "mainFocus", Bounds = new Rectangle(16, 16, 140, 24) };
    var standardClick = new StandardClickProbe { Name = "standardClick", Bounds = new Rectangle(180, 16, 40, 24) };
    var standardClicks = 0;
    standardClick.Click += (_, _) => ++standardClicks;
    main.Controls.Add(mainField);
    main.Controls.Add(standardClick);
    var secondary = new Form
    {
        Name = "secondaryPanel",
        Text = "Audio",
        Location = new Point(520, 410),
        Size = new Size(300, 260)
    };
    var nestedContentEnters = 0;
    var nestedContent = new Form
    {
        Name = "nestedDockContent",
        TopLevel = false,
        FormBorderStyle = FormBorderStyle.None,
        Bounds = new Rectangle(8, 30, 132, 70),
        TabIndex = 0,
        TabStop = true,
    };
    nestedContent.Enter += (_, _) => ++nestedContentEnters;
    var secondaryAction = new Button { Name = "secondaryAction", Text = "Apply", Bounds = new Rectangle(16, 108, 96, 26), TabIndex = 1 };
    secondary.Controls.Add(nestedContent);
    secondary.Controls.Add(secondaryAction);
    var propertyShown = new Form
    {
        Name = "visiblePropertyPanel",
        Text = "Property shown",
        Size = new Size(240, 180)
    };
    var propertyShownContent = new Panel
    {
        Name = "visiblePropertyContent",
        Bounds = new Rectangle(0, 0, 240, 180)
    };
    var propertyShownInner = new Panel { Bounds = new Rectangle(0, 0, 240, 180) };
    propertyShownContent.Controls.Add(propertyShownInner);
    propertyShown.Controls.Add(propertyShownContent);
    var loads = 0;
    var closes = 0;
    var cancelFirstClose = true;
    var closingReason = CloseReason.None;
    var closedReason = CloseReason.None;
    secondary.Load += (_, _) => ++loads;
    secondary.FormClosing += (_, e) => { closingReason = e.CloseReason; e.Cancel = cancelFirstClose; };
    secondary.FormClosed += (_, e) => { ++closes; closedReason = e.CloseReason; };
    main.Load += (_, _) =>
    {
        Require(mainField.Focus(), "main field accepts initial focus");
        secondary.Show();
        Require(ReferenceEquals(secondary.Parent, main) && secondary.Visible,
            "non-modal form attaches to the active retained host");
        Require(ReferenceEquals(secondary.Owner, main) && main.OwnedForms.Length == 1,
            "non-modal form records bidirectional ownership");
        Require(nestedContent.Focused && nestedContentEnters == 1,
            "hosted form gives its nested non-top-level form WinForms Enter focus semantics");
        Require(secondaryAction.Focus(), "secondary action remains focusable after nested form activation");
        Require(secondary.Right <= main.Width && secondary.Bottom <= main.Height,
            "non-modal form remains inside host bounds");
        var captionField = typeof(Form).GetField("__hostedCaptionPanel",
            global::System.Reflection.BindingFlags.Instance |
            global::System.Reflection.BindingFlags.NonPublic) ??
            throw new InvalidOperationException("hosted caption projection unavailable");
        var caption = captionField.GetValue(secondary) as Control ??
            throw new InvalidOperationException("hosted caption surface unavailable");
        var injectPointer = typeof(Control).GetMethod("__InjectManagedPointer",
            global::System.Reflection.BindingFlags.Instance |
            global::System.Reflection.BindingFlags.NonPublic) ??
            throw new InvalidOperationException("managed pointer injection unavailable");
        _ = injectPointer.Invoke(standardClick, new object[] { 6u, 10d, 10d, 0d, 1u });
        _ = injectPointer.Invoke(standardClick, new object[] { 7u, 10d, 10d, 0d, 0u });
        Require(standardClicks == 1,
            "standard Control click survives a buttonless native mouse-up");
        var beforeDrag = secondary.Bounds;
        _ = injectPointer.Invoke(caption, new object[] { 6u, 20d, 12d, 0d, 1u });
        _ = injectPointer.Invoke(caption, new object[] { 5u, -80d, -48d, 0d, 0u });
        _ = injectPointer.Invoke(caption, new object[] { 7u, -80d, -48d, 0d, 0u });
        Require(secondary.Left == beforeDrag.Left - 100 &&
            secondary.Top == beforeDrag.Top - 60 && !caption.Capture,
            "hosted caption drag moves the popup and releases capture on buttonless mouse-up");
        propertyShown.Visible = true;
        Require(ReferenceEquals(propertyShown.Parent, main) && propertyShown.Visible &&
            ReferenceEquals(propertyShown.Owner, main),
            "setting Visible presents a top-level form through the retained host");
        Require(propertyShownContent.Top == 0 && propertyShownContent.Padding.Top == 25 &&
            propertyShownInner.Top == 25 && propertyShownInner.Height == 155,
            "property-presented form reserves caption space outside its client content");
        Require(propertyShown.Controls.Count == 1 &&
            propertyShownContent.Controls.Count == 1,
            "hosted caption projection preserves the consumer form tree");
        var closeField = typeof(Form).GetField("__hostedCaptionClose",
            global::System.Reflection.BindingFlags.Instance |
            global::System.Reflection.BindingFlags.NonPublic) ??
            throw new InvalidOperationException("hosted caption close projection unavailable");
        var closeButton = closeField.GetValue(propertyShown) as Button ??
            throw new InvalidOperationException("hosted caption close button unavailable");
        Require(closeButton.Parent?.Name == "__guiFormsHostedCaption" &&
            closeButton.Parent.Parent?.Name == "__guiFormsHostedProjection",
            "hosted caption is projected into the topmost overlay");
        Require(closeButton.Visible,
            "hosted caption exposes a retained close hit target");
        closeButton.PerformClick();
        Require(propertyShown.Parent is null && !propertyShown.Visible,
            "hosted caption close detaches a property-presented form deterministically");
        secondary.Hide();
        secondary.Show();
        Require(loads == 1 && secondary.Visible, "non-modal form reopens without duplicate load");
        secondary.Close();
        Require(secondary.Visible && ReferenceEquals(secondary.Parent, main) && closes == 0 &&
            closingReason == CloseReason.UserClosing,
            "cancelled non-modal close preserves visibility and ownership");
        cancelFirstClose = false;
        secondary.Close();
        Require(secondary.Parent is null && !secondary.Visible && closes == 1,
            "non-modal form closes and detaches deterministically");
        Require(closedReason == CloseReason.UserClosing && mainField.Focused,
            "non-modal close reports reason and restores prior focus");
        main.BeginInvoke((Action)main.Close);
    };
    Application.Run(main);
    Console.WriteLine("secondary-form=attached:true|owned:true|clamped:true|reopened:true|cancelled:true|detached:true|focus-restored:true|loads:1|closed:1|standard-click:true");
    secondary.Dispose();
    propertyShownInner.Dispose();
    propertyShownContent.Dispose();
    propertyShown.Dispose();
    main.Dispose();
    return 0;
}

static int RunInheritedThemePaintHost()
{
    var main = new Form
    {
        Name = "themeMain",
        Text = "Theme inheritance",
        Size = new Size(520, 360),
    };
    var plugin = new Form
    {
        Name = "themePlugin",
        Text = "Plugin",
        Size = new Size(300, 220),
    };
    var container = new Panel
    {
        Name = "themeContainer",
        Dock = DockStyle.Fill,
    };
    var painted = new ThemePaintProbe
    {
        Name = "themePaintedPluginControl",
        Bounds = new Rectangle(12, 12, 180, 80),
    };
    container.Controls.Add(painted);
    plugin.Controls.Add(container);

    var snapshotMethod = typeof(Control).GetMethod("__ManagedPaintSurfaceSnapshot",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("managed paint diagnostics are unavailable");
    Dictionary<string, string> Snapshot() => ParseSurfaceSnapshot(
        (string)(snapshotMethod.Invoke(painted, null) ?? string.Empty));

    using var timer = new System.Windows.Forms.Timer { Interval = 40 };
    Dictionary<string, string>? before = null;
    Dictionary<string, string>? touched = null;
    var paintsBefore = 0;
    var ticks = 0;
    timer.Tick += (_, _) =>
    {
        ++ticks;
        if (ticks == 1)
        {
            painted.Update();
            before = Snapshot();
            paintsBefore = painted.PaintCount;
            plugin.BackColor = Color.FromArgb(24, 28, 33);
            plugin.ForeColor = Color.FromArgb(235, 240, 245);
            touched = Snapshot();
            Require(touched["state"] == "dirty_queued" &&
                SurfaceMetric(touched, "queued") == 1,
                "one callback atomically queues an inherited plugin repaint");
            return;
        }

        var after = Snapshot();
        Require(before is not null && touched is not null &&
            after["state"] == "clean" &&
            SurfaceMetric(after, "leases-completed") ==
                SurfaceMetric(before, "leases-completed") + 1 &&
            painted.PaintCount == paintsBefore + 1,
            "inherited theme colors publish one owner-painted plugin frame");
        Require(painted.LastBackColor == plugin.BackColor &&
            painted.LastForeColor == plugin.ForeColor,
            "owner-painted plugin observes the new inherited theme colors");
        timer.Stop();
        plugin.Close();
        main.Close();
    };
    main.Load += (_, _) =>
    {
        plugin.Show();
        timer.Start();
    };
    Application.Run(main);
    Require(ticks >= 2, $"theme change reaches its presentation boundary (ticks={ticks})");
    Require(Application.CallbackFaultCount == 0,
        $"theme change completes without callback faults ({Application.LastCallbackException})");
    Console.WriteLine("theme-inherited-paint=plugin:true|callback:atomic|frame:one|colors:current|faults:zero");
    painted.Dispose();
    container.Dispose();
    plugin.Dispose();
    main.Dispose();
    return 0;
}

static int RunBackgroundOnlyPaintHost()
{
    var form = new Form
    {
        Name = "backgroundOnlyMain",
        Text = "Background-only owner paint",
        BackColor = Color.FromArgb(43, 43, 43),
        Size = new Size(360, 220),
    };
    var surface = new BackgroundOnlyPanelProbe
    {
        Name = "backgroundOnlySurface",
        BackColor = Color.FromArgb(43, 43, 43),
        Bounds = new Rectangle(16, 16, 240, 140),
    };
    form.Controls.Add(surface);
    surface.Update();
    Require(surface.Backgrounds > 0,
        "a container overriding only OnPaintBackground owns a managed raster surface");
    Require(surface.LastPaintedColor == Color.FromArgb(25, 25, 25) &&
        surface.BackColor == Color.FromArgb(43, 43, 43),
        "background-only owner paint can project an application surface distinct from BackColor");
    Console.WriteLine($"background-only-paint=backgrounds:{surface.Backgrounds}|managed:rgb25|property:rgb43");
    surface.Dispose();
    form.Dispose();
    return 0;
}

static int RunThemeRestoreTransactionHost()
{
    var form = new Form
    {
        Name = "themeTransactionMain",
        Text = "Theme restore transaction",
        Size = new Size(720, 480),
    };
    var panel = new Panel { Dock = DockStyle.Fill };
    var probes = new List<ThemePaintProbe>();
    for (var index = 0; index < 24; ++index)
    {
        var probe = new ThemePaintProbe
        {
            Name = "themeTransactionProbe" + index,
            Bounds = new Rectangle(8 + index % 6 * 112, 8 + index / 6 * 96, 104, 88),
        };
        probes.Add(probe);
        panel.Controls.Add(probe);
    }
    form.Controls.Add(panel);

    using var timer = new System.Windows.Forms.Timer { Interval = 40 };
    int[]? beforeRestore = null;
    var ticks = 0;
    timer.Tick += (_, _) =>
    {
        ++ticks;
        if (ticks == 1)
        {
            form.Visible = false;
            panel.SuspendLayout();
            for (var index = 0; index < probes.Count; ++index)
            {
                probes[index].BackColor = index % 2 == 0 ? Color.FromArgb(24, 28, 33) : Color.FromArgb(36, 40, 45);
                probes[index].ForeColor = Color.FromArgb(235, 240, 245);
            }
            panel.ResumeLayout(true);
            beforeRestore = probes.Select(probe => probe.PaintCount).ToArray();
            form.Visible = true;
            Require(probes.Select(probe => probe.PaintCount).SequenceEqual(beforeRestore),
                "theme visibility restore must queue rather than synchronously replay descendant paint");
            return;
        }

        Require(beforeRestore is not null, "theme restore baseline exists");
        Require(probes.Select((probe, index) => probe.PaintCount == beforeRestore![index] + 1).All(value => value),
            "theme restore publishes one coalesced final paint per raster descendant");
        timer.Stop();
        form.Close();
    };
    form.Load += (_, _) => timer.Start();
    Application.Run(form);
    Require(ticks >= 2, $"theme restore reaches one later presentation turn (ticks={ticks})");
    Require(Application.CallbackFaultCount == 0,
        $"theme restore completes without callback faults ({Application.LastCallbackException})");
    Console.WriteLine("theme-restore-transaction=restore:queued|descendants:coalesced|callback:returned|faults:zero");
    foreach (var probe in probes) probe.Dispose();
    panel.Dispose();
    form.Dispose();
    return 0;
}

static int RunSplitContainerSemantics()
{
    var form = new Form { Name = "splitHost", Text = "Split container", ClientSize = new Size(420, 220) };
    var split = new SplitContainerProbe
    {
        Name = "splitProbe",
        Dock = DockStyle.Fill,
        SplitterWidth = 3,
        Panel1MinSize = 80,
        Panel2MinSize = 100,
        SplitterDistance = 120
    };
    var first = new Button { Name = "splitFirst", Text = "First pane", Bounds = new Rectangle(8, 8, 90, 26) };
    var second = new Button { Name = "splitSecond", Text = "Second pane", Bounds = new Rectangle(8, 8, 100, 26) };
    split.Panel1.Controls.Add(first);
    split.Panel2.Controls.Add(second);
    form.Controls.Add(split);
    form.Load += (_, _) =>
    {
        split.PerformLayout();
        Require(split.Panel1.Width == 120 && split.Panel2.Left == 123 &&
            split.Panel2.Width == split.ClientSize.Width - 123,
            "split layout allocates both panels around its physical seam");
        split.DragSplitter(121, 181);
        Require(split.SplitterDistance == 180 && split.Panel1.Width == 180 && split.Panel2.Left == 183,
            "split pointer drag updates pane allocation before release");
        Require(first.Focus(), "split child accepts focus before collapse");
        split.Panel1Collapsed = true;
        Require(!split.Panel1.Visible && split.Focused &&
            split.Panel2.Width == split.ClientSize.Width - 3,
            "collapsing a focused pane transfers focus and removes it from layout");
        split.Panel1Collapsed = false;
        split.SplitterDistance = 150;
        split.Orientation = Orientation.Horizontal;
        split.SplitterDistance = 70;
        split.PerformLayout();
        Require(split.SplitterDistance == 80 && split.Panel1.Height == 80 &&
            split.Panel2.Top == 83,
            "horizontal split orientation divides the vertical axis while preserving minima");
        split.IsSplitterFixed = true;
        split.DragSplitter(71, 120);
        Require(split.SplitterDistance == 80,
            "fixed splitter rejects pointer mutation");
        form.BeginInvoke((Action)form.Close);
    };
    Application.Run(form);
    Console.WriteLine("split-container=geometry:constrained|drag:live|collapse:focus-transferred|orientation:horizontal|fixed:enforced");
    form.Dispose();
    return 0;
}

static int RunFormSemantics()
{
    var form = new DialogKeyProbe { Name = "formSemantics", Size = new Size(420, 220), KeyPreview = true };
    var group = new Panel { Name = "nestedFields", Bounds = new Rectangle(12, 12, 220, 80) };
    var later = new TextBox { Name = "later", TabIndex = 2, Bounds = new Rectangle(4, 4, 120, 22) };
    var earlier = new TextBox { Name = "earlier", TabIndex = 1, Bounds = new Rectangle(4, 34, 120, 22) };
    group.Controls.Add(later);
    group.Controls.Add(earlier);
    var accept = new Button { Name = "accept", Text = "OK", TabIndex = 3, DialogResult = DialogResult.OK };
    var cancel = new Button { Name = "cancel", Text = "Cancel", TabIndex = 4, DialogResult = DialogResult.Cancel };
    form.Controls.Add(group);
    form.Controls.Add(accept);
    form.Controls.Add(cancel);
    form.AcceptButton = accept;
    form.CancelButton = cancel;

    Require(later.Focus() && form.Route(Keys.Shift | Keys.Tab) && earlier.Focused,
        "reverse tab follows nested stable TabIndex order");
    Require(form.Route(Keys.Tab) && later.Focused,
        "forward tab follows nested stable TabIndex order");
    form.ActiveControl = earlier;
    Require(ReferenceEquals(form.ActiveControl, earlier), "active-control setter focuses descendants");
    var outside = new TextBox();
    var rejectedOutside = false;
    try { form.ActiveControl = outside; }
    catch (ArgumentException) { rejectedOutside = true; }
    Require(rejectedOutside, "active-control setter rejects foreign controls");
    var label = new Label();
    Require(!label.Focus(), "noninteractive label rejects keyboard focus");

    var order = new System.Collections.Generic.List<string>();
    accept.Click += (_, _) => order.Add("accept-click");
    cancel.Click += (_, _) => order.Add("cancel-click");
    Require(form.Route(Keys.Enter) && form.DialogResult == DialogResult.OK &&
        order.Count == 1 && order[0] == "accept-click",
        "enter performs accept click before applying dialog result");
    form.DialogResult = DialogResult.None;
    Require(form.Route(Keys.Escape) && form.DialogResult == DialogResult.Cancel &&
        order.Count == 2 && order[1] == "cancel-click",
        "escape performs cancel click and applies dialog result");

    var owner = new Form();
    form.Owner = owner;
    Require(ReferenceEquals(form.Owner, owner) && owner.OwnedForms.Length == 1,
        "owned-form relationship is bidirectional");
    var cycleRejected = false;
    try { owner.Owner = form; }
    catch (ArgumentException) { cycleRejected = true; }
    Require(cycleRejected, "owned-form cycle rejected");

    var modalOwner = new Form { Name = "modalOwner", Size = new Size(320, 180) };
    var ownerField = new TextBox { Name = "ownerField", Bounds = new Rectangle(8, 8, 120, 22) };
    modalOwner.Controls.Add(ownerField);
    Require(ownerField.Focus(), "modal owner establishes prior focus");
    var dialog = new Form { Name = "modalChild", Size = new Size(260, 140) };
    var dialogField = new TextBox { Name = "dialogField", Bounds = new Rectangle(8, 8, 120, 22) };
    dialog.Controls.Add(dialogField);
    var ownerSuppressedDuringLoad = false;
    dialog.Load += (_, _) =>
    {
        ownerSuppressedDuringLoad = !modalOwner.Enabled && !ownerField.Focused;
        dialog.BeginInvoke((Action)(() => dialog.DialogResult = DialogResult.OK));
    };
    var modalResult = dialog.ShowDialog(modalOwner);
    Require(ownerSuppressedDuringLoad && modalResult == DialogResult.OK &&
        modalOwner.Enabled && ownerField.Focused && !dialog.Visible,
        "modal owner suppression and focus restoration");

    Console.WriteLine("form-semantics=tab:nested|accept:ordered|cancel:ordered|active-control:scoped|owner:acyclic|modal:focus-restored|label-focus:rejected");
    outside.Dispose();
    label.Dispose();
    dialog.Dispose();
    modalOwner.Dispose();
    form.Dispose();
    owner.Dispose();
    return 0;
}

static int RunCursorSemantics()
{
    using var panel = new Panel { Name = "cursorPanel" };
    Require(ReferenceEquals(Cursors.VSplit, Cursors.SizeWE),
        "vertical splitter and west-east resize cursors share retained identity");
    Require(!ReferenceEquals(Cursors.HSplit, Cursors.VSplit) &&
        !ReferenceEquals(Cursors.Default, Cursors.Hand),
        "distinct cursor roles retain distinct identity");
    panel.Cursor = Cursors.Hand;
    Require(ReferenceEquals(panel.Cursor, Cursors.Hand),
        "control cursor round trips through ABI projection");
    panel.Cursor = Cursors.VSplit;
    Require(ReferenceEquals(panel.Cursor, Cursors.VSplit),
        "control cursor role can change");
    panel.Cursor = null!;
    Require(panel.Cursor is null, "null cursor restores inherited native state");
    Console.WriteLine("cursor=identity:stable|projection:roundtrip|inherit:restored");
    return 0;
}

static int RunDockPaddingSemantics()
{
    using var panel = new ScrollableControl { Name = "dockPaddingPanel", Size = new Size(200, 100) };
    using var child = new Panel { Name = "dockFill", Dock = DockStyle.Fill };
    panel.Controls.Add(child);
    panel.DockPadding.All = 4;
    panel.DockPadding.Left = 11;
    panel.PerformLayout();
    Require(panel.Padding.Left == 11 && panel.Padding.Top == 4 &&
        panel.Padding.Right == 4 && panel.Padding.Bottom == 4,
        "dock padding projects into retained Padding");
    Require(child.Bounds == new Rectangle(11, 4, 185, 92),
        "fill layout consumes dock padding edges");
    panel.DockPadding.Bottom = 9;
    panel.PerformLayout();
    Require(child.Bounds == new Rectangle(11, 4, 185, 87),
        "dock padding mutation relayouts the owner");
    Console.WriteLine("dock-padding=projection:owned|fill:inset|relayout:updated");
    return 0;
}

static int RunInitialDockShowSemantics()
{
    using var form = new Form
    {
        Name = "initialDockForm",
        Text = "Initial dock show",
        ClientSize = new Size(360, 220),
    };
    using var toolbar = new Panel
    {
        Name = "initialToolbar",
        Dock = DockStyle.Top,
        Height = 36,
    };
    using var content = new Panel
    {
        Name = "initialContent",
        Dock = DockStyle.Fill,
    };

    // This is the standard designer sequence: author while suspended, discard
    // the stale queued request, and let first presentation perform a fresh
    // layout from the final property values.
    form.SuspendLayout();
    form.Controls.Add(content);
    form.Controls.Add(toolbar);
    form.ResumeLayout(false);

    var committedBeforeLoad = false;
    form.Load += (_, _) =>
    {
        committedBeforeLoad = toolbar.Bounds == new Rectangle(0, 0, 360, 36) &&
            content.Bounds == new Rectangle(0, 36, 360, 184);
    };
    var priorAutoClose = Environment.GetEnvironmentVariable("GUI_FORMS_AUTOMATION_CLOSE");
    Environment.SetEnvironmentVariable("GUI_FORMS_AUTOMATION_CLOSE", "1");
    try { Application.Run(form); }
    finally { Environment.SetEnvironmentVariable("GUI_FORMS_AUTOMATION_CLOSE", priorAutoClose); }

    Require(committedBeforeLoad,
        "initial presentation must commit current Dock geometry before Load");
    Console.WriteLine("initial-dock-show=resume-false:fresh|toolbar:reserved|fill:remaining|before-load:true");
    return 0;
}

static int RunControlGeometrySemantics()
{
    using var root = new GeometryPanel { Name = "geometryRoot", Size = new Size(200, 140) };
    using var back = new GeometryPanel {
        Name = "geometryBack", Bounds = new Rectangle(10, 12, 80, 60), TabIndex = 20 };
    using var front = new GeometryPanel {
        Name = "geometryFront", Bounds = new Rectangle(10, 12, 80, 60), TabIndex = 10 };
    using var nested = new Panel {
        Name = "geometryNested", Bounds = new Rectangle(2, 3, 12, 9), TabIndex = 5 };
    root.Controls.Add(front);
    root.Controls.Add(back);
    front.Controls.Add(nested);

    Require(root.Controls.GetChildIndex(front) == 0 &&
        root.Controls.GetChildIndex(back) == 1 &&
        ReferenceEquals(root.GetChildAtPoint(new Point(20, 20)), front),
        "ControlCollection and point lookup expose topmost-first z order");
    front.Visible = false;
    Require(ReferenceEquals(root.GetChildAtPoint(
        new Point(20, 20), GetChildAtPointSkip.Invisible), back),
        "invisible point lookup reveals the next retained child");
    front.Visible = true;
    front.Enabled = false;
    Require(ReferenceEquals(root.GetChildAtPoint(
        new Point(20, 20), GetChildAtPointSkip.Disabled), back),
        "disabled point lookup reveals the next retained child");
    front.Enabled = true;
    front.MakeTransparent();
    Require(ReferenceEquals(root.GetChildAtPoint(
        new Point(20, 20), GetChildAtPointSkip.Transparent), back),
        "transparent point lookup uses the retained transparency contract");
    front.MakeOpaque();

    front.SendToBack();
    Require(root.Controls.GetChildIndex(front) == 1 &&
        ReferenceEquals(root.GetChildAtPoint(new Point(20, 20)), back),
        "SendToBack updates both collection and hit-test order");
    front.BringToFront();
    Require(root.Controls.GetChildIndex(front) == 0,
        "BringToFront restores topmost collection order");

    front.SetBounds(25, 30, 70, 40,
        BoundsSpecified.Location | BoundsSpecified.Width);
    Require(front.Bounds == new Rectangle(25, 30, 70, 60),
        "masked bounds mutation preserves unspecified height");
    Require(front.RectangleToScreen(new Rectangle(2, 3, 4, 5)) ==
            new Rectangle(27, 33, 4, 5) &&
        front.RectangleToClient(new Rectangle(27, 33, 4, 5)) ==
            new Rectangle(2, 3, 4, 5),
        "point and rectangle transforms round trip retained ancestry");
    Require(ReferenceEquals(root.GetNextControl(null!, true), front) &&
        ReferenceEquals(root.GetNextControl(front, true), nested) &&
        ReferenceEquals(root.GetNextControl(nested, true), back) &&
        root.GetNextControl(back, true) is null,
        "GetNextControl traverses stable nested TabIndex order without wrapping");
    using var pointer = new PointerPositionProbe {
        Name = "geometryPointer", Bounds = new Rectangle(40, 50, 30, 20) };
    root.Controls.Add(pointer);
    pointer.InjectDown(7, 9);
    Require(pointer.LastClientMousePosition == new Point(7, 9) &&
        Control.MousePosition == new Point(47, 59),
        "native pointer ingress publishes WinForms screen MousePosition before callbacks");

    using var sizing = new GeometryPanel {
        Name = "geometryAutoSize", Size = new Size(100, 80),
        Padding = new Padding(2) };
    using var content = new Panel {
        Name = "geometryContent", Bounds = new Rectangle(10, 8, 40, 20) };
    sizing.Controls.Add(content);
    var autoSizeEvents = 0;
    sizing.AutoSizeChanged += (_, _) => ++autoSizeEvents;
    sizing.SetMode(AutoSizeMode.GrowAndShrink);
    sizing.AutoSize = true;
    Require(sizing.PreferredSize == new Size(55, 33) &&
        sizing.Size == new Size(55, 33) && autoSizeEvents == 1,
        "GrowAndShrink AutoSize derives child, margin, and padding extent once");

    using var docking = new Panel { Name = "dockOrder", Size = new Size(200, 100) };
    using var dockLeft = new Panel { Name = "dockLeft", Width = 50, Dock = DockStyle.Left };
    using var dockFill = new Panel { Name = "dockFill", Dock = DockStyle.Fill };
    docking.Controls.Add(dockFill);
    docking.Controls.Add(dockLeft);
    docking.PerformLayout();
    Require(dockLeft.Bounds == new Rectangle(0, 0, 50, 100) &&
        dockFill.Bounds == new Rectangle(50, 0, 150, 100),
        "docking consumes client space from backmost to topmost z order");

    using var autoDockOwner = new Panel { Name = "autoDockOwner", Size = new Size(240, 100) };
    using var autoDock = new GeometryPanel {
        Name = "autoDockTop", Size = new Size(400, 0), Dock = DockStyle.Top };
    using var autoDockChild = new Panel {
        Name = "autoDockChild", Height = 30, Dock = DockStyle.Top };
    autoDock.SetMode(AutoSizeMode.GrowAndShrink);
    autoDock.AutoSize = true;
    autoDock.Controls.Add(autoDockChild);
    autoDockOwner.Controls.Add(autoDock);
    autoDockOwner.PerformLayout();
    autoDock.PerformLayout();
    Require(autoDock.Bounds == new Rectangle(0, 0, 240, 30),
        $"Dock Top constrains AutoSize width while content determines height; actual={autoDock.Bounds}");

    Console.WriteLine("control-geometry=zorder:coherent|lookup:filtered|bounds:masked|coordinates:roundtrip|mouse-position:screen|tab-order:nested|autosize:shrink|dock:zorder");
    return 0;
}

static int RunPropertyGridProjection()
{
    using var grid = new PropertyGrid();
    using var first = new Label { Name = "first", Text = "Alpha" };
    using var second = new Label { Name = "second", Text = "Beta" };
    var selectionChanges = 0;
    var sortChanges = 0;
    grid.SelectedObjectsChanged += (_, _) => ++selectionChanges;
    grid.PropertySortChanged += (_, _) => ++sortChanges;
    grid.SelectedObjects = new object[] { first, second };
    var copy = grid.SelectedObjects;
    copy[0] = second;
    var cloned = ReferenceEquals(grid.SelectedObject, first);
    grid.PropertySort = PropertySort.Alphabetical;
    grid.Refresh();
    var managedFirst = new ManagedPropertyFixture { Gain = 12, Mode = ReceiverMode.Fast, Level = new ReceiverLevel(2), RejectValue = 3 };
    var managedSecond = new ManagedPropertyFixture { Gain = 12, Mode = ReceiverMode.Fast, Level = new ReceiverLevel(2), RejectValue = 3, RejectNine = true };
    grid.SelectedObjects = new object[] { managedFirst, managedSecond };
    grid.Refresh();
    var arbitraryProjected = ReferenceEquals(grid.SelectedObject, managedFirst) &&
        grid.SelectedObjects.Length == 2 && managedFirst.GetterCalls > 0 &&
        managedSecond.GetterCalls > 0 && ReceiverLevelConverter.FormatCalls > 0 &&
        ReceiverLevelConverter.LastCultureName ==
            global::System.Globalization.CultureInfo.CurrentCulture.Name;
    var trySet = typeof(PropertyGrid).GetMethod("__TrySetPropertyText",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("PropertyGrid text automation seam is unavailable");
    var reset = typeof(PropertyGrid).GetMethod("__ResetProperty",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("PropertyGrid reset automation seam is unavailable");
    var edit = typeof(PropertyGrid).GetMethod("__EditProperty",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("PropertyGrid editor automation seam is unavailable");
    var proxySchemaClean = !(bool)trySet.Invoke(grid, new object[] { "Visible", "False" })!;
    var dropDownEditor = (bool)edit.Invoke(grid, new object[] { "Mode" })! &&
        managedFirst.Mode == ReceiverMode.Precise &&
        managedSecond.Mode == ReceiverMode.Precise &&
        ReceiverModeEditor.DropDownCalls == 1 &&
        ReceiverModeEditor.CloseCalls == 1;
    var modalEditor = (bool)edit.Invoke(grid, new object[] { "RejectValue" })! &&
        managedFirst.RejectValue == 4 && managedSecond.RejectValue == 4 &&
        ReceiverValueEditor.ModalCalls == 1 &&
        ReceiverValueEditor.AcceptedCalls == 1;
    var convertedCommit = (bool)trySet.Invoke(grid, new object[] { "Level", "1" })! &&
        managedFirst.Level.Value == 1 && managedSecond.Level.Value == 1;
    var nullableCommit = (bool)trySet.Invoke(grid, new object[] { "Gain", "" })! &&
        managedFirst.Gain is null && managedSecond.Gain is null;
    var resetCommit = (bool)trySet.Invoke(grid, new object[] { "Gain", "6" })! &&
        (bool)reset.Invoke(grid, new object[] { "Gain" })! &&
        managedFirst.Gain is null && managedSecond.Gain is null;
    var rejectingCommit = (bool)trySet.Invoke(grid, new object[] { "RejectValue", "9" })!;
    var atomicRollback = !rejectingCommit && managedFirst.RejectValue == 4 &&
        managedSecond.RejectValue == 4;
    var arbitraryRejected = false;
    try { grid.SelectedObject = new UnsupportedPropertyFixture(); }
    catch (NotSupportedException) { arbitraryRejected = true; }
    var retainedAfterFailure = ReferenceEquals(grid.SelectedObject, managedFirst) &&
        grid.SelectedObjects.Length == 2;
    grid.SelectedObject = null!;
    var cleared = grid.SelectedObject is null && grid.SelectedObjects.Length == 0;
    Console.WriteLine("property-grid=native:true|multi:true|clone:" +
        cloned.ToString().ToLowerInvariant() + "|sort:" +
        (grid.PropertySort == PropertySort.Alphabetical).ToString().ToLowerInvariant() +
        "|selection-events:" + selectionChanges + "|sort-events:" + sortChanges +
        "|type-descriptor:" + arbitraryProjected.ToString().ToLowerInvariant() +
        "|proxy-schema-clean:" + proxySchemaClean.ToString().ToLowerInvariant() +
        "|dropdown-editor:" + dropDownEditor.ToString().ToLowerInvariant() +
        "|modal-editor:" + modalEditor.ToString().ToLowerInvariant() +
        "|converted-commit:" + convertedCommit.ToString().ToLowerInvariant() +
        "|nullable-commit:" + nullableCommit.ToString().ToLowerInvariant() +
        "|reset:" + resetCommit.ToString().ToLowerInvariant() +
        "|atomic-rollback:" + atomicRollback.ToString().ToLowerInvariant() +
        "|arbitrary-rejected:" + arbitraryRejected.ToString().ToLowerInvariant() +
        "|retained-after-failure:" + retainedAfterFailure.ToString().ToLowerInvariant() +
        "|cleared:" + cleared.ToString().ToLowerInvariant());
    return cloned && arbitraryProjected && proxySchemaClean && dropDownEditor && modalEditor && convertedCommit && nullableCommit &&
        resetCommit && atomicRollback && arbitraryRejected &&
        retainedAfterFailure && cleared && selectionChanges == 3 &&
        sortChanges == 1 ? 0 : 1;
}

static int RunLayoutTransactionSemantics()
{
    using var probe = new TransactionLayoutProbe
    {
        Name = "layoutTransactionProbe",
        Size = new Size(240, 120),
    };
    using var fill = new Panel { Name = "layoutFill", Dock = DockStyle.Fill };
    probe.Controls.Add(fill);
    probe.PerformLayout();
    probe.Reset();

    probe.SuspendLayout();
    probe.SuspendLayout();
    probe.Padding = new Padding(4);
    probe.Padding = new Padding(8, 6, 10, 12);
    probe.PerformLayout(fill, "Bounds");
    Require(probe.Layouts == 0,
        "nested SuspendLayout must preserve the managed committed layout");
    probe.ResumeLayout(true);
    Require(probe.Layouts == 0,
        "an inner ResumeLayout must not run a managed layout pass");

    probe.Reentries = 2;
    probe.ResumeLayout(true);
    Require(probe.Layouts == 3,
        "re-entrant PerformLayout requests must become bounded deferred passes");
    Require(ReferenceEquals(probe.FirstAffectedControl, fill) &&
            probe.FirstAffectedProperty == "Bounds",
        "PerformLayout(Control,string) must preserve the triggering arguments");
    Require(fill.Bounds == new Rectangle(8, 6, 222, 102),
        "the final coalesced pass must publish docked geometry");

    var committedPasses = probe.Layouts;
    probe.SuspendLayout();
    probe.Padding = new Padding(3);
    probe.PerformLayout(fill, "Padding");
    probe.ResumeLayout(false);
    Require(probe.Layouts == committedPasses,
        "ResumeLayout(false) must not dispatch a stale cached layout event");
    probe.PerformLayout(fill, "Explicit");
    Require(probe.Layouts == committedPasses + 1 &&
            fill.Bounds == new Rectangle(3, 3, 234, 114),
        "an explicit later PerformLayout must flush current geometry once");
    probe.ResumeLayout(true);
    Require(probe.Layouts == committedPasses + 1,
        "an unmatched ResumeLayout must be a no-op");

    var beforeFault = probe.Layouts;
    probe.ThrowNext = true;
    var faultObserved = false;
    try { probe.PerformLayout(fill, "Fault"); }
    catch (InvalidOperationException) { faultObserved = true; }
    Require(faultObserved && probe.Layouts == beforeFault + 1,
        "a layout callback fault must propagate exactly once");
    probe.PerformLayout(fill, "Recovery");
    Require(probe.Layouts == beforeFault + 2,
        "a layout callback fault must not wedge later layout");

    Console.WriteLine("layout-transactions=nested:coalesced|committed:stable|args:exact|reentry:bounded|resume-false:deferred|unmatched:no-op|fault:recoverable");
    return 0;
}

static int RunDialogKeyLiveHost()
{
    var form = new Form { Name = "dialogKeyLive", Text = "Dialog key routing", Size = new Size(360, 180) };
    var field = new TextBox { Name = "dialogField", Bounds = new Rectangle(16, 18, 180, 24), TabIndex = 1 };
    var accept = new Button { Name = "dialogAccept", Text = "OK", Bounds = new Rectangle(16, 62, 80, 26), DialogResult = DialogResult.OK, TabIndex = 2 };
    var cancel = new Button { Name = "dialogCancel", Text = "Cancel", Bounds = new Rectangle(104, 62, 80, 26), DialogResult = DialogResult.Cancel, TabIndex = 3 };
    form.Controls.Add(field);
    form.Controls.Add(accept);
    form.Controls.Add(cancel);
    form.AcceptButton = accept;
    form.CancelButton = cancel;
    var accepts = 0;
    var cancels = 0;
    accept.Click += (_, _) => ++accepts;
    cancel.Click += (_, _) => ++cancels;
    form.Load += (_, _) => field.Focus();
    Application.Run(form);
    Require(accepts == 1 && cancels == 1 && form.DialogResult == DialogResult.Cancel,
        "physical form preview routes enter and escape once");
    Console.WriteLine($"dialog-key-live=enter:{accepts}|escape:{cancels}|focused-field:true|result:{form.DialogResult}");
    form.Dispose();
    return 0;
}

static int RunScrollablePanelHost()
{
    using var painted = new ScrollablePaintProbe
    {
        Name = "paintedScrollViewport",
        AutoScroll = true,
        Size = new Size(120, 70),
    };
    painted.Controls.Add(new Control
    {
        Name = "paintedScrollContent",
        Bounds = new Rectangle(4, 140, 40, 20),
    });
    Require(painted.VerticalScroll.Visible &&
        painted.DisplayRectangle.Height >= 160,
        "owner painting and scrolling must compose on one retained control");

    var panel = new ScrollProbe { Name = "scrollViewport", AutoScroll = true, Size = new Size(240, 120) };
    var upper = new Button { Name = "upperSetting", Text = "Upper", Bounds = new Rectangle(8, 8, 100, 24) };
    var lower = new Button { Name = "lowerSetting", Text = "Lower", Bounds = new Rectangle(8, 260, 100, 24) };
    panel.Controls.Add(upper);
    panel.Controls.Add(lower);
    var authoredUpper = upper.Bounds;
    var authoredLower = lower.Bounds;
    Require(lower.Top == 260 && panel.VerticalScroll.Visible &&
        panel.DisplayRectangle.Height >= lower.Bottom,
        "scroll fixture begins below a measured automatic viewport");
    panel.Wheel(-120);
    Require(panel.AutoScrollPosition.Y == -48 && lower.Bounds == authoredLower &&
        upper.Bounds == authoredUpper && panel.DisplayRectangle.Y == -48,
        "scroll wheel moves the viewport without mutating authored child bounds");
    for (var index = 0; index < 12; ++index) panel.Wheel(-120);
    var maximumPosition = -panel.AutoScrollPosition.Y;
    Require(maximumPosition > 48 && lower.Bottom - maximumPosition <= panel.Height,
        "scroll wheel reaches the final retained setting");
    panel.Wheel(120);
    Require(-panel.AutoScrollPosition.Y < maximumPosition &&
        lower.Bounds == authoredLower, "scroll wheel reverses deterministically");

    lower.AutoScrollOffset = new Point(3, 5);
    panel.AutoScrollPosition = Point.Empty;
    panel.ScrollControlIntoView(lower);
    Require(panel.AutoScrollPosition.X == 0 && panel.AutoScrollPosition.Y < 0 &&
        lower.Bounds == authoredLower,
        "ScrollControlIntoView honors the child reveal offset without geometry drift");

    panel.AutoScroll = false;
    panel.VerticalScroll.Maximum = 250;
    panel.VerticalScroll.LargeChange = 50;
    panel.VerticalScroll.SmallChange = 1;
    panel.VerticalScroll.Visible = true;
    panel.VerticalScroll.Value = 80;
    Require(panel.VerticalScroll.Enabled && panel.VerticalScroll.Visible &&
        panel.VerticalScroll.Minimum == 0 && panel.VerticalScroll.Maximum == 250 &&
        panel.VerticalScroll.LargeChange == 50 &&
        panel.VerticalScroll.SmallChange == 1 && panel.VerticalScroll.Value == 80,
        "manual ScrollProperties round-trip through the retained ABI");

    var eventArgs = new ScrollEventArgs(ScrollEventType.ThumbTrack, 12, 34,
        ScrollOrientation.VerticalScroll);
    eventArgs.NewValue = 35;
    var eventCalls = 0;
    ScrollEventArgs? delivered = null;
    panel.Scroll += (_, args) => { ++eventCalls; delivered = args; };
    panel.RaiseScroll(eventArgs);
    Require(eventArgs.Type == ScrollEventType.ThumbTrack &&
        eventArgs.OldValue == 12 && eventArgs.NewValue == 35 &&
        eventArgs.ScrollOrientation == ScrollOrientation.VerticalScroll &&
        eventCalls == 1 && ReferenceEquals(delivered, eventArgs),
        "ScrollEventArgs preserves exact event vocabulary and mutable new value");
    Console.WriteLine("scroll-panel=retained:true|paint-composed:true|step:48|reached:true|reverse:true|into-view:true|manual-axis:true|event-args:true|event-delivery:true");
    panel.Dispose();
    return 0;
}

static int RunScrollablePanelLiveHost()
{
    var form = new Form
    {
        Name = "scrollLiveForm",
        Text = "GUI.Forms Scroll Event",
        ClientSize = new Size(260, 160),
    };
    var panel = new Panel
    {
        Name = "scrollViewport",
        AutoScroll = true,
        Bounds = new Rectangle(10, 10, 240, 120),
    };
    panel.Controls.Add(new Button
    {
        Name = "scrollLiveContent",
        Text = "Below viewport",
        Bounds = new Rectangle(8, 260, 120, 24),
    });
    form.Controls.Add(panel);
    var calls = 0;
    ScrollEventArgs? delivered = null;
    panel.Scroll += (_, args) => { ++calls; delivered = args; };
    Application.Run(form);
    Require(calls == 1 && delivered is not null &&
        delivered.Type == ScrollEventType.SmallIncrement &&
        delivered.ScrollOrientation == ScrollOrientation.VerticalScroll &&
        delivered.NewValue > delivered.OldValue && panel.AutoScrollPosition.Y < 0,
        "physical scrollbar input must cross ABI 0.20 into one managed Scroll event");
    Console.WriteLine($"scroll-panel-live=events:{calls}|type:{delivered!.Type}|orientation:vertical|position:{-panel.AutoScrollPosition.Y}");
    form.Dispose();
    return 0;
}

static int RunNumericEditHost()
{
    var numeric = new NumericProbe
    {
        Name = "editableNumeric",
        Minimum = -100m,
        Maximum = 100m,
        DecimalPlaces = 1,
        Value = 12m,
        Size = new Size(120, 24)
    };
    var changes = 0;
    numeric.ValueChanged += (_, _) => ++changes;
    var keyInput = typeof(NumericUpDown).GetMethod("__NativeKeyInput",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!;
    var textInput = typeof(NumericUpDown).GetMethod("__NativeTextInput",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!;
    keyInput.Invoke(numeric, [0x04u, true, 2u, false]);
    textInput.Invoke(numeric, ["45.5", false, -1, 0]);
    Require(numeric.Value == 45.5m && numeric.Text == "45.5", "numeric control-a replacement");
    keyInput.Invoke(numeric, [0x2au, true, 0u, false]);
    Require(numeric.Value == 45m && numeric.Text == "45.", "numeric backspace preserves editable decimal state");
    keyInput.Invoke(numeric, [0x04u, true, 2u, false]);
    textInput.Invoke(numeric, ["-7.5", false, -1, 0]);
    Require(numeric.Value == -7.5m && numeric.Text == "-7.5", "numeric signed text replacement");
    keyInput.Invoke(numeric, [0x28u, true, 0u, false]);
    Require(numeric.Text == "-7.5", "numeric enter commit formats deterministically");
    Require(changes == 3, "numeric text edits raise value changes only for parsed changes");
    numeric.Value = 12m;
    numeric.DragSelect(5, 19);
    textInput.Invoke(numeric, ["7", false, -1, 0]);
    Require(numeric.Value == 7m && numeric.Text == "7.0", "numeric mouse drag replacement");
    Console.WriteLine("numeric-edit=select-all:true|replace:true|backspace:true|signed:true|commit:true|drag:true|changes:5");
    numeric.Dispose();
    return 0;
}

static int RunNumericTopology()
{
    using var numeric = new NumericUpDown();
    Require(numeric.Controls.Count == 2, "numeric child count");
    Require(numeric.Controls[0] is Button && numeric.Controls[0].Name == "upDownButtons",
        "numeric spinner child topology");
    Require(numeric.Controls[1] is TextBox && numeric.Controls[1].Name == "upDownEdit",
        "numeric edit child topology");
    Console.WriteLine("numeric-topology=spinner:0|edit:1|theme-cast:non-null");
    return 0;
}

static int RunFormIconContract()
{
    using var form = new Form();
    var icon = form.Icon;
    Require(icon is not null, "default form icon");
    using (var bitmap = icon!.ToBitmap())
        Require(bitmap.Width == 32 && bitmap.Height == 32, "default form icon decodes");
    form.Icon = null!;
    Require(form.Icon is not null, "null form icon restores default");
    Console.WriteLine("form-icon=default:present|decode:32x32|null:restored");
    return 0;
}

static int RunConstructorCallbackOrder()
{
    using var probe = new ConstructionReentryProbe();
    Require(probe.EarlyResizeCalls == 0 && probe.EarlyLayoutCalls == 0,
        "native initialization must not enter derived resize or layout overrides");
    var textChanges = 0;
    var visibleChanges = 0;
    var enabledChanges = 0;
    probe.TextChanged += (_, _) => ++textChanges;
    probe.VisibleChanged += (_, _) => ++visibleChanges;
    probe.EnabledChanged += (_, _) => ++enabledChanges;
    probe.Text = "ready";
    probe.Visible = false;
    probe.Enabled = false;
    Require(textChanges == 1 && visibleChanges == 1 && enabledChanges == 1,
        "managed property mutations must raise one precise managed event");
    probe.Size = new Size(200, 120);
    probe.PerformLayout();
    Require(probe.ResizeCalls >= 1 && probe.LayoutCalls >= 1,
        "post-construction resize and layout must remain available");
    Console.WriteLine($"constructor-callback-order=early-resize:{probe.EarlyResizeCalls}|early-layout:{probe.EarlyLayoutCalls}|text:{textChanges}|visible:{visibleChanges}|enabled:{enabledChanges}");
    return 0;
}

static int RunTextEditHost()
{
    var field = new TextBox { Name = "clipboardField", Text = "alpha beta" };
    var keyInput = typeof(TextBoxBase).GetMethod("__NativeKeyInput",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic)!;
    var setSelection = typeof(TextBoxBase).GetMethod("__SetTextSelection",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic)!;
    var textInput = typeof(TextBoxBase).GetMethod("__NativeTextInput",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic)!;
    var selectionStart = typeof(TextBoxBase).GetProperty("__TextSelectionStart",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic)!;
    var selectionLength = typeof(TextBoxBase).GetProperty("__TextSelectionLength",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic)!;
    setSelection.Invoke(field, [0, 5]);
    keyInput.Invoke(field, [0x06u, true, 2u, false]);
    Require(Clipboard.GetText() == "alpha", "text copy command");
    setSelection.Invoke(field, [6, 4]);
    keyInput.Invoke(field, [0x1bu, true, 2u, false]);
    Require(field.Text == "alpha " && Clipboard.GetText() == "beta",
        "text cut command");
    setSelection.Invoke(field, [field.Text.Length, 0]);
    keyInput.Invoke(field, [0x19u, true, 2u, false]);
    Require(field.Text == "alpha beta", "text paste command");
    setSelection.Invoke(field, [0, 5]);
    field.ReadOnly = true;
    keyInput.Invoke(field, [0x06u, true, 2u, false]);
    Require(Clipboard.GetText() == "alpha", "read-only copy command");
    keyInput.Invoke(field, [0x1bu, true, 2u, false]);
    Require(field.Text == "alpha beta", "read-only cut rejected");
    field.ReadOnly = false;
    field.Text = "a\u0301b";
    setSelection.Invoke(field, [2, 0]);
    keyInput.Invoke(field, [0x2au, true, 0u, false]);
    Require(field.Text == "b", "combining grapheme backspace");
    field.Text = "😀z";
    setSelection.Invoke(field, [2, 0]);
    keyInput.Invoke(field, [0x50u, true, 1u, false]);
    keyInput.Invoke(field, [0x4cu, true, 0u, false]);
    Require(field.Text == "z", "surrogate grapheme selection delete");
    field.Text = "start";
    setSelection.Invoke(field, [1, 3]);
    var textChanges = 0;
    field.TextChanged += (_, _) => ++textChanges;
    textInput.Invoke(field, ["X", false, -1, 0]);
    Require(field.Text == "sXt" && (int)selectionStart.GetValue(field)! == 2 &&
        (int)selectionLength.GetValue(field)! == 0,
        "native edit transaction projects text and caret");
    keyInput.Invoke(field, [0x1du, true, 2u, false]);
    Require(field.Text == "start" && (int)selectionStart.GetValue(field)! == 1 &&
        (int)selectionLength.GetValue(field)! == 3,
        "native undo restores text and selection");
    keyInput.Invoke(field, [0x1cu, true, 2u, false]);
    Require(field.Text == "sXt" && (int)selectionStart.GetValue(field)! == 2 &&
        (int)selectionLength.GetValue(field)! == 0,
        "native redo restores text and caret");
    Require(textChanges == 3, "native edit, undo, and redo each raise one text change");
    Console.WriteLine("text-edit=copy:true|cut:true|paste:true|readonly-copy:true|grapheme:true|native-edit:true|undo:true|redo:true|events:3");
    field.Dispose();
    return 0;
}

static int RunMenuHost()
{
    var form = new Form { Name = "menuForm", Size = new Size(700, 220) };
    var menu = new MenuProbe { Name = "contextMenu" };
    using var icon = new Bitmap(32, 32);
    using (var iconGraphics = Graphics.FromImage(icon)) iconGraphics.Clear(Color.SteelBlue);
    var connect = new ToolStripMenuItem("Connect") { Image = icon, Size = new Size(112, 22) };
    var clicked = 0;
    connect.Click += (_, _) => ++clicked;
    var source = new ToolStripMenuItem("Source") { Image = icon, Size = new Size(112, 22) };
    var nestedClicked = 0;
    var server = new ToolStripMenuItem("AIRSPY Server Network");
    server.Click += (_, _) => ++nestedClicked;
    source.DropDownItems.Add(server);
    menu.Items.Add(connect);
    menu.Items.Add(new ToolStripSeparator());
    menu.Items.Add(source);
    menu.Show(form, new Point(24, 36));
    Require(menu.Bounds.X == 24 && menu.Bounds.Y == 36, "menu screen position");
    Require(menu.Width >= 136 && menu.Height >= 61, "menu retained vertical extent");
    Require(source.DropDown.OwnerItem == source && source.DropDownItems.Count == 1,
        "nested menu ownership");
    using var raster = menu.RenderToBitmap();
    var glyphPixels = 0;
    var nonBackgroundPixels = 0;
    for (var y = 7; y < Math.Min(26, raster.Height - 2); ++y)
    for (var x = 39; x < Math.Min(raster.Width - 18, 112); ++x)
    {
        var pixel = raster.GetPixel(x, y);
        if (pixel.R != 247 || pixel.G != 249 || pixel.B != 252) ++nonBackgroundPixels;
        if (pixel.A > 0 && pixel.R < 100 && pixel.G < 100 && pixel.B < 100) ++glyphPixels;
    }
    var capturePath = Environment.GetEnvironmentVariable("GUI_FORMS_CAPTURE_MENU");
    if (!string.IsNullOrEmpty(capturePath)) raster.Save(capturePath);
    Console.WriteLine($"menu-raster=glyph-pixels:{glyphPixels}|non-background:{nonBackgroundPixels}|capture:{capturePath ?? "none"}");
    Require(glyphPixels >= 3 && nonBackgroundPixels >= 40, "menu text glyph raster");
    menu.ReleaseAt(50, 40);
    Require(menu.Visible && source.DropDown.Visible && source.DropDown.Parent == form,
        "nested menu retained with parent open");
    Require(source.DropDown.Left >= menu.Right - 3,
        "nested menu placed beside parent");
    typeof(ToolStrip).GetMethod("OnMouseUp",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!
        .Invoke(source.DropDown, [new MouseEventArgs(MouseButtons.Left, 1, 50, 12, 0)]);
    Require(nestedClicked == 1 && !source.DropDown.Visible && source.DropDown.Parent is null &&
        !menu.Visible && menu.Parent is null, "nested leaf activation closes menu chain");
    menu.Show(form, new Point(24, 36));
    menu.ReleaseAt(50, 12);
    Require(clicked == 1 && !menu.Visible && menu.Parent is null,
        "root leaf activation and dismissal");

    menu.Show(form, new Point(form.Width - 8, 36));
    Require(menu.Right == form.Width, "root menu clamps to retained form edge");
    menu.Key(Keys.End);
    menu.Key(Keys.Right);
    Require(source.DropDown.Visible && source.DropDown.Right <= menu.Left,
        "keyboard branch opens with leftward edge reversal");
    PressKey(source.DropDown, Keys.Left);
    Require(menu.Visible && !source.DropDown.Visible,
        "left closes one keyboard menu level");
    menu.Key(Keys.End);
    menu.Key(Keys.Right);
    PressKey(source.DropDown, Keys.Enter);
    Require(nestedClicked == 2 && !menu.Visible,
        "keyboard nested activation closes complete chain");

    menu.Show(form, new Point(24, 36));
    menu.Key(Keys.Home);
    menu.Key(Keys.Enter);
    Require(clicked == 2 && !menu.Visible, "keyboard root activation");
    menu.Show(form, new Point(24, 36));
    menu.Key(Keys.Escape);
    Require(!menu.Visible && menu.Parent is null, "escape dismisses root menu");

    var baselineChildren = form.Controls.Count;
    for (var cycle = 0; cycle < 24; ++cycle)
    {
        menu.Show(form, new Point(24, 36));
        Require(form.Controls.Count == baselineChildren + 1, "one retained popup per cycle");
        menu.Key(Keys.Escape);
        Require(form.Controls.Count == baselineChildren && menu.Parent is null,
            "popup cycle detaches without retained children");
    }
    var multilineMenu = new MenuProbe { Name = "multilineMenu" };
    multilineMenu.Items.Add(new ToolStripMenuItem(
        "OH3BHX - oh3bhx.fi\r\nReceiver: Airspy R2\r\nCoverage: 24 MHz - 1.8 GHz"));
    multilineMenu.Show(form, new Point(24, 36));
    Require(multilineMenu.Height >= 57,
        "multiline menu text expands the retained row instead of painting CR/LF glyphs");
    using var multilineRaster = multilineMenu.RenderToBitmap();
    Require(multilineRaster.Height == multilineMenu.Height,
        "multiline menu raster covers the expanded retained row");
    multilineMenu.Key(Keys.Escape);
    multilineMenu.Dispose();
    Console.WriteLine($"menu=bounds:{menu.Bounds.X},{menu.Bounds.Y},{menu.Width},{menu.Height}|items:{menu.Items.Count}|nested:{source.DropDownItems.Count}|nested-clicked:{nestedClicked}|clicked:{clicked}|glyph-pixels:{glyphPixels}|multiline:expanded|keyboard:pass|edge:left|cycles:24");
    menu.Dispose();
    form.Dispose();
    return 0;
}

static int RunComboHost()
{
    var form = new Form { Name = "comboForm", Size = new Size(360, 180) };
    var combo = new ComboProbe
    {
        Name = "serverUri",
        Bounds = new Rectangle(20, 20, 240, 24),
        DropDownStyle = ComboBoxStyle.DropDownList,
    };
    combo.Items.AddRange(["sdr://airspy.com:5555", "sdr://localhost:5555"]);
    combo.SelectedIndex = 0;
    var capabilityCombo = new ComboBox();
    capabilityCombo.Items.AddRange(["660 kHz", "330 kHz"]);
    capabilityCombo.SelectedIndex = 1;
    var capabilitySelectionEvents = 0;
    capabilityCombo.SelectedIndexChanged += (_, _) => ++capabilitySelectionEvents;
    capabilityCombo.Items.Clear();
    Require(capabilityCombo.SelectedIndex == -1 && capabilitySelectionEvents == 0,
        "combo item reset suppresses transient selection event");
    capabilityCombo.Items.AddRange(["660 kHz", "330 kHz"]);
    capabilityCombo.SelectedIndex = 0;
    Require(capabilitySelectionEvents == 1,
        "combo explicit post-reset selection event");

    var streamFormatCombo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
    streamFormatCombo.Items.AddRange([
        "SHARP IQ", "SHARP IQ legacy", "32 Bit Float", "24 Bit PCM", "16 Bit PCM",
        "8 Bit PCM", "PCM 4-bit", "PCM 2-bit", "PCM 1-bit",
    ]);
    streamFormatCombo.SelectedIndex = 5;
    var selectedFormat = streamFormatCombo.SelectedItem;
    streamFormatCombo.Items.Insert(0, "future format");
    Require(streamFormatCombo.SelectedIndex == 6 && ReferenceEquals(streamFormatCombo.SelectedItem, selectedFormat),
        "combo insert preserves selected object");
    streamFormatCombo.Items.RemoveAt(0);
    Require(streamFormatCombo.SelectedIndex == 5 && ReferenceEquals(streamFormatCombo.SelectedItem, selectedFormat),
        "combo remove before selection preserves selected object");
    var rejectedOutOfRange = false;
    try { streamFormatCombo.SelectedIndex = streamFormatCombo.Items.Count; }
    catch (ArgumentOutOfRangeException) { rejectedOutOfRange = true; }
    Require(rejectedOutOfRange && streamFormatCombo.SelectedIndex == 5,
        "combo rejects impossible positive selection without mutation");
    var opened = 0;
    var closed = 0;
    combo.DropDown += (_, _) => ++opened;
    combo.DropDownClosed += (_, _) => ++closed;
    form.Controls.Add(combo);

    var editable = new ComboProbe { Text = "sdr://old.example:5555", Size = new Size(240, 24) };
    var comboKeyInput = typeof(ComboBox).GetMethod("__NativeKeyInput",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!;
    var comboTextInput = typeof(ComboBox).GetMethod("__NativeTextInput",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!;
    comboTextInput.Invoke(editable, ["x", false, -1, 0]);
    Require(editable.Text == "sdr://old.example:5555x", "editable combo caret defaults to end");
    comboKeyInput.Invoke(editable, [0x04u, true, 2u, false]);
    comboTextInput.Invoke(editable, ["sdr://new.example:5555", false, -1, 0]);
    Require(editable.Text == "sdr://new.example:5555", "editable combo control-a replacement");
    comboKeyInput.Invoke(editable, [0x2au, true, 0u, false]);
    Require(editable.Text == "sdr://new.example:555", "editable combo backspace");
    editable.Select(0, editable.Text.Length);
    Require(editable.SelectedIndex == -1, "editable combo remains a free-text value");
    comboTextInput.Invoke(editable, ["sdr://192.168.1.96:5555/", false, -1, 0]);
    comboKeyInput.Invoke(editable, [0x4au, true, 0u, false]);
    comboKeyInput.Invoke(editable, [0x4cu, true, 0u, false]);
    Require(editable.Text == "dr://192.168.1.96:5555/", "editable combo home and delete");
    comboKeyInput.Invoke(editable, [0x4du, true, 0u, false]);
    comboKeyInput.Invoke(editable, [0x50u, true, 1u, false]);
    comboTextInput.Invoke(editable, ["x", false, -1, 0]);
    Require(editable.Text == "dr://192.168.1.96:5555x", "editable combo shift selection replacement");
    editable.Text = "abcdef";
    editable.DragSelect(12, 33);
    var comboSelectionStart = (int)typeof(ComboBox).GetProperty("__ComboSelectionStart",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!.GetValue(editable)!;
    var comboSelectionLength = (int)typeof(ComboBox).GetProperty("__ComboSelectionLength",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!.GetValue(editable)!;
    Require(comboSelectionStart == 1 && comboSelectionLength == 3,
        "editable combo mouse drag preserves an anchor and live range");

    var textBox = new TextProbe { Text = "field", Size = new Size(240, 24) };
    var textKeyInput = typeof(TextBox).GetMethod("__NativeKeyInput",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!;
    var textInput = typeof(TextBox).GetMethod("__NativeTextInput",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!;
    textKeyInput.Invoke(textBox, [0x04u, true, 2u, false]);
    textInput.Invoke(textBox, ["replacement", false, -1, 0]);
    textKeyInput.Invoke(textBox, [0x4au, true, 0u, false]);
    textKeyInput.Invoke(textBox, [0x4fu, true, 0u, false]);
    textKeyInput.Invoke(textBox, [0x2au, true, 0u, false]);
    Require(textBox.Text == "eplacement", "text box select-all, replace, navigation, and backspace");
    textBox.Text = "abcdef";
    textBox.DragSelect(12, 33);
    var textSelectionStart = (int)typeof(TextBoxBase).GetProperty("__TextSelectionStart",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!.GetValue(textBox)!;
    var textSelectionLength = (int)typeof(TextBoxBase).GetProperty("__TextSelectionLength",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!.GetValue(textBox)!;
    Require(textSelectionStart == 1 && textSelectionLength == 3,
        "text box mouse drag preserves an anchor and live range");

    combo.ReleaseAt(combo.Width - 6, combo.Height / 2);
    ContextMenuStrip? dropDown = null;
    foreach (Control child in form.Controls)
        if (child is ContextMenuStrip candidate) dropDown = candidate;
    Require(dropDown is not null && dropDown.Visible && dropDown.Parent == form,
        "combo retained owned drop-down");
    var activeDropDown = dropDown ?? throw new InvalidOperationException("combo drop-down missing");
    typeof(Application).GetMethod("__CloseActiveMenu",
        global::System.Reflection.BindingFlags.Static | global::System.Reflection.BindingFlags.NonPublic)!.Invoke(null, null);
    Require(!activeDropDown.Visible && activeDropDown.Parent is null && opened == 1 && closed == 1,
        "outside dismissal releases combo drop-down state");
    combo.ReleaseAt(combo.Width - 6, combo.Height / 2);
    dropDown = null;
    foreach (Control child in form.Controls)
        if (child is ContextMenuStrip candidate) dropDown = candidate;
    activeDropDown = dropDown ?? throw new InvalidOperationException("reopened combo drop-down missing");
    typeof(ToolStrip).GetMethod("OnMouseUp",
        global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!
        .Invoke(activeDropDown, [new MouseEventArgs(MouseButtons.Left, 1, 50, 34, 0)]);
    Require(combo.SelectedIndex == 1 && combo.Text == "sdr://localhost:5555",
        "combo drop-down selection");
    Require(opened == 2 && closed == 2 && !activeDropDown.Visible && activeDropDown.Parent is null,
        "combo drop-down lifecycle");
    Console.WriteLine($"combo=opened:{opened}|closed:{closed}|selected:{combo.SelectedIndex}|text:{combo.Text}|editable:{editable.Text}|field:{textBox.Text}|reset-events:{capabilitySelectionEvents}|stream-format:{streamFormatCombo.SelectedIndex}|bounds:pass|retained:true");
    capabilityCombo.Dispose();
    streamFormatCombo.Dispose();
    editable.Dispose();
    textBox.Dispose();
    form.Dispose();
    return 0;
}

static int RunFieldLiveHost()
{
    var form = new Form
    {
        Name = "fieldForm",
        Text = "GUI.Forms field input",
        Size = new Size(520, 220),
    };
    var editable = new ComboBox
    {
        Name = "editableUri",
        Text = "sdr://old.example:5555/",
        Bounds = new Rectangle(20, 24, 440, 26),
        DropDownStyle = ComboBoxStyle.DropDown,
    };
    editable.Items.AddRange(["sdr://localhost:5555/", "sdr://192.168.1.96:5555/"]);
    var field = new TextBox
    {
        Name = "plainField",
        Text = "replace me",
        Bounds = new Rectangle(20, 70, 440, 26),
    };
    var numeric = new NumericUpDown
    {
        Name = "editableNumeric",
        Minimum = -100m,
        Maximum = 100m,
        DecimalPlaces = 1,
        Value = 12m,
        Bounds = new Rectangle(20, 116, 180, 26),
    };
    form.Controls.Add(editable);
    form.Controls.Add(field);
    form.Controls.Add(numeric);
    Application.Run(form);
    Console.WriteLine($"field-live=combo:{editable.Text}|text:{field.Text}|numeric:{numeric.Text}|value:{numeric.Value}");
    return 0;
}

static int RunCrossThreadFontHost()
{
    Exception? workerFailure = null;
    var worker = new Thread(() =>
    {
        try
        {
            using var bitmap = new Bitmap(8, 8);
            using var graphics = Graphics.FromImage(bitmap);
            graphics.Clear(Color.White);
            using var stream = new System.IO.MemoryStream();
            bitmap.Save(stream, System.Drawing.Imaging.ImageFormat.Png);
        }
        catch (Exception error) { workerFailure = error; }
    });
    worker.Start();
    worker.Join();
    if (workerFailure is not null) throw workerFailure;

    using var raster = new Bitmap(180, 32);
    using (var graphics = Graphics.FromImage(raster))
    using (var font = new Font("Portsmouth Rapids", 12f, FontStyle.Regular, GraphicsUnit.Pixel, 1))
    using (var ink = new SolidBrush(Color.FromArgb(31, 37, 44)))
    {
        graphics.Clear(Color.White);
        graphics.DrawString("AIRSPY Server", font, ink, 3f, 3f);
    }
    var darkPixels = 0;
    for (var y = 0; y < raster.Height; ++y)
    for (var x = 0; x < raster.Width; ++x)
    {
        var pixel = raster.GetPixel(x, y);
        if (pixel.R < 120 && pixel.G < 120 && pixel.B < 120) ++darkPixels;
    }
    Require(darkPixels >= 8, "thread-local raster font registration");
    Console.WriteLine($"font-thread=worker-init:true|ui-glyph-pixels:{darkPixels}");
    return 0;
}

static int RunSliderLiveHost()
{
    var form = new Form
    {
        Name = "sliderLiveForm",
        Text = "GUI.Forms drag gate",
        Size = new Size(420, 170),
    };
    var slider = new DragSliderProbe
    {
        Name = "dragSlider",
        Bounds = new Rectangle(30, 45, 330, 34),
    };
    form.Controls.Add(slider);
    Application.Run(form);
    Require(slider.MouseDowns == 1 && slider.MouseUps == 1,
        "slider receives one drag boundary");
    Require(slider.DragMoves >= 2 && slider.Value >= 90,
        "held-button moves drive slider to the requested end");
    Require(!slider.Capture, "slider releases managed capture");
    Console.WriteLine($"slider-live=value:{slider.Value}|down:{slider.MouseDowns}|moves:{slider.DragMoves}|up:{slider.MouseUps}|capture:{slider.Capture}");
    form.Dispose();
    return 0;
}

static int RunMessageBoxHost()
{
    var result = MessageBox.Show("Retained GUI.Forms message", "GUI.Forms",
        MessageBoxButtons.OKCancel, MessageBoxIcon.Information,
        MessageBoxDefaultButton.Button1);
    Require(result == DialogResult.OK, "message box first-button activation");
    Console.WriteLine($"messagebox=result:{result}|retained:true");
    return 0;
}

static int RunNativeDialogLiveHost()
{
    var form = new Form { Name = "nativeDialogForm", Text = "GUI.Forms dialog gate", Size = new Size(420, 170) };
    var open = new Button { Name = "openPicker", Text = "Open file picker", Bounds = new Rectangle(24, 42, 150, 30) };
    var result = DialogResult.None;
    open.Click += (_, _) =>
    {
        using var picker = new OpenFileDialog
        {
            Filter = "Images (*.png)|*.png|All files (*.*)|*.*",
            InitialDirectory = Environment.CurrentDirectory,
        };
        result = picker.ShowDialog(form);
        form.Close();
    };
    form.Controls.Add(open);
    Application.Run(form);
    Console.WriteLine($"dialog-live=result:{result}|provider:native");
    form.Dispose();
    return 0;
}

static int RunTooltipLiveHost()
{
    var form = new Form { Name = "tooltipForm", Text = "GUI.Forms tooltip gate", Size = new Size(420, 170) };
    var target = new Button { Name = "tooltipTarget", Text = "Hover for details", Bounds = new Rectangle(24, 42, 150, 30) };
    using var tooltip = new ToolTip
    {
        InitialDelay = 100,
        AutoPopDelay = 3000,
        ReshowDelay = 50,
    };
    tooltip.SetToolTip(target, "Rendered by the GUI.Forms non-activating popup adapter.");
    form.Controls.Add(target);
    Application.Run(form);
    form.Dispose();
    return 0;
}

static int RunNativeFolderDialogLiveHost()
{
    var form = new Form { Name = "nativeFolderForm", Text = "GUI.Forms folder gate", Size = new Size(420, 170) };
    var open = new Button { Name = "openFolderPicker", Text = "Select folder", Bounds = new Rectangle(24, 42, 150, 30) };
    var result = DialogResult.None;
    open.Click += (_, _) =>
    {
        using var picker = new FolderBrowserDialog { SelectedPath = Environment.CurrentDirectory };
        result = picker.ShowDialog(form);
        form.Close();
    };
    form.Controls.Add(open);
    Application.Run(form);
    Console.WriteLine($"folder-live=result:{result}|provider:native");
    form.Dispose();
    return 0;
}

static int RunOverlayMenuHost()
{
    var form = new Form { Name = "overlayMenuForm", Text = "Retained menu overlay", Size = new Size(360, 220) };
    var open = new Button { Name = "openMenuButton", Text = "Menu", Bounds = new Rectangle(20, 20, 90, 28) };
    var continueButton = new Button { Name = "continueButton", Text = "Continue", Bounds = new Rectangle(130, 20, 100, 28) };
    var menu = new ContextMenuStrip { Name = "overlayMenu" };
    var source = new ToolStripMenuItem("Source");
    source.DropDownItems.Add(new ToolStripMenuItem("AIRSPY Server Network"));
    var childShows = 0;
    var childHides = 0;
    source.DropDown.VisibleChanged += (_, _) =>
    {
        if (source.DropDown.Visible) ++childShows;
        else ++childHides;
    };
    menu.Items.Add(source);
    menu.Items.Add(new ToolStripMenuItem("Radio"));
    var showReturned = false;
    var continued = 0;
    open.Click += (_, _) => { menu.Show(open, 0, open.Height); showReturned = true; };
    continueButton.Click += (_, _) =>
    {
        Require(childShows == 1 && childHides >= 1 && !menu.Visible && menu.Parent is null &&
            !source.DropDown.Visible && source.DropDown.Parent is null,
            "menu chain dismisses before target click");
        ++continued;
        form.Close();
    };
    form.Controls.Add(open);
    form.Controls.Add(continueButton);
    Application.Run(form);
    Require(showReturned && continued == 1, "modeless retained menu overlay");
    Console.WriteLine($"menu-live=returned:{showReturned}|continued:{continued}|keyboard-child:{childShows}/{childHides}|outside-dismissed:{!menu.Visible && menu.Parent is null}");
    menu.Dispose();
    form.Dispose();
    return 0;
}

static int RunHoverMenuHost()
{
    var form = new Form { Name = "hoverMenuForm", Text = "Retained hover menu", Size = new Size(360, 220) };
    var menu = new MenuProbe { Name = "hoverMenu" };
    menu.Items.Add(new ToolStripMenuItem("Connect"));
    menu.Items.Add(new ToolStripSeparator());
    var source = new ToolStripMenuItem("Source");
    source.DropDownItems.Add(new ToolStripMenuItem("AIRSPY Server Network"));
    menu.Items.Add(source);
    var stage = 0;
    using var timer = new System.Windows.Forms.Timer { Interval = 550 };
    timer.Tick += (_, _) =>
    {
        if (stage == 0)
        {
            Require(source.DropDown.Visible, "hover delay opens branch");
            Require(source.DropDown.Right <= menu.Left, "hover branch reverses at retained edge");
            menu.MoveAt(50, 12);
            stage = 1;
            return;
        }
        Require(!source.DropDown.Visible, "hovering a leaf retires prior branch after delay");
        timer.Stop();
        menu.Key(Keys.Escape);
        form.Close();
        stage = 2;
    };
    form.Load += (_, _) =>
    {
        menu.Show(form, new Point(form.Width - 8, 36));
        menu.MoveAt(50, 40);
        timer.Start();
    };
    Application.Run(form);
    Require(stage == 2 && !menu.Visible && menu.Parent is null,
        "hover menu lifecycle completes");
    Console.WriteLine("menu-hover=open:delayed|close:delayed|edge:left|lifecycle:clean");
    menu.Dispose();
    form.Dispose();
    return 0;
}

static int RunPopupRepeatHost()
{
    const int cycles = 12;
    var loads = 0;
    var clicks = 0;
    for (var cycle = 0; cycle < cycles; ++cycle)
    {
        using var dialog = new Form
        {
            Name = "repeatDialog" + cycle,
            Text = "Popup lifecycle " + cycle,
            Size = new Size(320, 160),
        };
        using var popup = new MenuProbe { Name = "repeatPopup" + cycle };
        var close = new ToolStripMenuItem("Close");
        close.Click += (_, _) => ++clicks;
        popup.Items.Add(close);
        dialog.Load += (_, _) =>
        {
            ++loads;
            popup.Show(dialog, new Point(20, 28));
            popup.ReleaseAt(50, 12);
            Require(!popup.Visible && popup.Parent is null,
                "popup detaches before repeated dialog close");
            dialog.BeginInvoke((Action)dialog.Close);
        };
        _ = dialog.ShowDialog();
        Require(!dialog.Visible && popup.Parent is null,
            "repeated dialog and popup close cleanly");
    }
    Require(loads == cycles && clicks == cycles,
        "repeated popup/dialog callbacks exactly once");
    Console.WriteLine($"popup-repeat=cycles:{cycles}|loads:{loads}|clicks:{clicks}|retained-orphans:0");
    return 0;
}

static void PressKey(Control control, Keys key) =>
    typeof(Control).GetMethod("OnKeyDown",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic)!
        .Invoke(control, [new KeyEventArgs(key)]);

static int RunLoadLifecycle()
{
    var form = new Form { Name = "loadForm", Size = new Size(360, 180) };
    var initial = new LoadProbe();
    var nestedPaintHost = new Panel { Size = new Size(80, 40) };
    var nestedPaint = new PaintInputProbe { Dock = DockStyle.Fill };
    nestedPaintHost.Controls.Add(nestedPaint);
    LoadProbe? late = null;
    var formLoads = 0;
    form.Controls.Add(initial);
    form.Controls.Add(nestedPaintHost);
    form.Load += (_, _) =>
    {
        ++formLoads;
        late = new LoadProbe();
        form.Controls.Add(late);
    };
    Application.Run(form);
    Require(formLoads == 1, "form load once");
    Require(initial.Loads == 1, "initial child load once");
    var attachedLate = late ?? throw new InvalidOperationException("M11d behavior check failed: late child created");
    Require(attachedLate.Loads == 1, "late child load once");
    Require(nestedPaint.Paints >= 2, "final form-load pass repaints nested custom surfaces");
    form.Show();
    Require(formLoads == 1 && initial.Loads == 1 && attachedLate.Loads == 1, "load idempotence");
    Console.WriteLine($"lifecycle=form:{formLoads}|initial:{initial.Loads}|late:{attachedLate.Loads}");
    form.Dispose();
    return 0;
}

static int RunInitializationOrder()
{
    Environment.SetEnvironmentVariable("GUI_FORMS_AUTOMATION_ACTIVATE", "1");
    var form = new Form { Name = "initializationOrder", Size = new Size(360, 180) };
    var button = new Button { Name = "initializationAction", Dock = DockStyle.Fill };
    form.Controls.Add(button);
    var order = new System.Collections.Generic.List<string>();
    var handleCreated = 0;
    var handleDestroyed = 0;
    form.HandleCreated += (_, _) => ++handleCreated;
    form.HandleDestroyed += (_, _) => ++handleDestroyed;
    form.Load += (_, _) => order.Add("load");
    button.Click += (_, _) =>
    {
        order.Add("input");
        form.Close();
    };
    form.FormClosing += (_, _) => order.Add("closing");
    form.FormClosed += (_, _) => order.Add("closed");

    if (OperatingSystem.IsWindows())
    {
        Require(!form.IsHandleCreated, "compatibility handle begins unleased");
        Require(form.Handle != 0 && form.IsHandleCreated && handleCreated == 1,
            "compatibility handle acquisition raises HandleCreated exactly once");
        _ = form.Handle;
        Require(handleCreated == 1, "re-reading a compatibility handle is idempotent");
    }

    Application.Run(form);
    var observedOrder = string.Join(",", order);
    Require(observedOrder == "load,input,closing,closed",
        $"portable initialization completes before input and terminal callbacks (observed {observedOrder})");
    form.Dispose();
    if (OperatingSystem.IsWindows())
        Require(handleDestroyed == 1, "disposing a leased handle raises HandleDestroyed once");

    var early = new Form { Name = "earlyCloseInitialization", Size = new Size(240, 120) };
    var forbiddenInput = new Button { Name = "earlyCloseInput", Dock = DockStyle.Fill };
    early.Controls.Add(forbiddenInput);
    var earlyClosed = 0;
    forbiddenInput.Click += (_, _) => throw new InvalidOperationException(
        "input escaped after Load requested close");
    early.Load += (_, _) => early.Close();
    early.FormClosed += (_, _) => ++earlyClosed;
    Application.Run(early);
    Require(earlyClosed == 1,
        "Load-time close suppresses input and reaches one terminal callback");
    early.Dispose();
    Console.WriteLine($"lifecycle-order={string.Join('>', order)}|handle-created:{handleCreated}|handle-destroyed:{handleDestroyed}|early-close:suppressed");
    return 0;
}

static int RunPointerHost()
{
    var form = new Form { Name = "pointerForm", Size = new Size(360, 180) };
    var probe = new PaintInputProbe { Dock = DockStyle.Fill };
    form.Controls.Add(probe);
    Application.Run(form);
    Require(probe.Paints > 0, "owner paint lifecycle");
    Require(probe.MouseDowns == 1 && probe.MouseUps == 1, "owner pointer delivery");
    Require(probe.LastPoint.X >= 0 && probe.LastPoint.Y >= 0, "owner pointer coordinates");
    Console.WriteLine($"pointer=paint:{probe.Paints}|down:{probe.MouseDowns}|up:{probe.MouseUps}|point:{probe.LastPoint.X},{probe.LastPoint.Y}");
    form.Dispose();
    return 0;
}

static int RunRasterButtonHost()
{
    var form = new Form { Name = "buttonForm", Size = new Size(360, 180) };
    var button = new Button
    {
        Name = "imageButton",
        Bounds = new Rectangle(20, 20, 80, 40),
        Text = "Run",
    };
    var clicks = 0;
    button.Click += (_, _) => ++clicks;
    form.Controls.Add(button);
    Application.Run(form);
    Require(clicks == 1, "raster button click delivery");
    Console.WriteLine($"button=click:{clicks}|surface:raster");
    form.Dispose();
    return 0;
}

static int RunDialogHost()
{
    var dialog = new Form { Name = "dialogForm", Text = "M11g dialog", Size = new Size(360, 180) };
    var map = new Control { Name = "dialogMap", Dock = DockStyle.Fill };
    dialog.Controls.Add(map);
    var loads = 0;
    var popupClicked = 0;
    ContextMenuStrip? popup = null;
    dialog.Load += (_, _) =>
    {
        ++loads;
        popup = new ContextMenuStrip { Name = "serverChoices" };
        var choice = new ToolStripMenuItem("Server A");
        choice.Click += (_, _) => ++popupClicked;
        popup.Items.Add(choice);
        popup.Show(new Point(24, 36));
        Require(popup.Parent == dialog && popup.Visible, "unowned popup attaches to active dialog");
        typeof(ToolStrip).GetMethod("OnMouseUp",
            global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic)!
            .Invoke(popup, [new MouseEventArgs(MouseButtons.Left, 1, 50, 12, 0)]);
    };
    var result = dialog.ShowDialog();
    Require(loads == 1, "dialog load once");
    Require(!dialog.Visible && result == DialogResult.None, "dialog close result");
    Require(popupClicked == 1 && popup is not null && !popup.Visible && popup.Parent is null,
        "active dialog popup leaf dispatch and dismissal");
    Exception? refreshError = null;
    var refreshThread = new Thread(() =>
    {
        try { map.Refresh(); }
        catch (Exception error) { refreshError = error; }
    });
    refreshThread.Start();
    Require(refreshThread.Join(TimeSpan.FromSeconds(2)), "closed dialog refresh returns");
    Require(refreshError is null, $"closed dialog refresh no-op ({refreshError?.GetType().Name})");
    Console.WriteLine($"dialog=loads:{loads}|result:{result}|visible:{dialog.Visible}|popup-clicked:{popupClicked}|closed-refresh:no-op");
    popup?.Dispose();
    dialog.Dispose();
    return 0;
}

static int RunNativeWindowSurfaceHost()
{
    var traceSurface = Environment.GetEnvironmentVariable("GUI_FORMS_TRACE_NATIVE_SURFACES") == "1";
    void TraceSurface(string stage)
    {
        if (traceSurface) Console.Error.WriteLine("native-surface-stage=" + stage);
    }
    TraceSurface("construct-form");
    var form = new Form { Name = "nativeSurfaceForm", Text = "Native surface", Size = new Size(240, 140) };
    TraceSurface("construct-surface");
    var surface = new PaintInputProbe
    {
        Name = "nativeSurface",
        Bounds = new Rectangle(12, 12, 80, 40),
        BackColor = Color.Black,
    };
    TraceSurface("attach-surface");
    form.Controls.Add(surface);
    TraceSurface("request-handle");
    var window = surface.Handle;
    TraceSurface("validate-handle");
    Require(window != 0 && NativeSurfaceProbe.IsWindow(window), "control handle is a Win32 window");
    TraceSurface("get-construction-dc");
    var constructionDevice = NativeSurfaceProbe.GetDC(window);
    TraceSurface("validate-construction-dc");
    Require(constructionDevice != 0,
        "control HWND is compositor-backed before Handle escapes");
    Require(!NativeSurfaceProbe.IsWindowEnabled(window),
        "paint-only child HWND cannot become a second input authority");
    surface.Size = new Size(96, 48);
    TraceSurface("validate-resize");
    Require(NativeSurfaceProbe.GetClientRect(window, out var resized) &&
        resized.Right - resized.Left >= 96 && resized.Bottom - resized.Top >= 48,
        "control HWND reserves durable retained-HDC capacity");

    var snapshotMethod = typeof(Control).GetMethod("__WindowSurfaceSnapshot",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("window-surface diagnostics are unavailable");
    string Snapshot() => (string)(snapshotMethod.Invoke(surface, null) ?? string.Empty);

    using var timer = new System.Windows.Forms.Timer { Interval = 40 };
    var ticks = 0;
    var paintedCompatibilitySurface = false;
    Dictionary<string, string>? before = null;
    Dictionary<string, string>? during = null;
    Dictionary<string, string>? after = null;
    Exception? failure = null;
    var newestReached = false;
    timer.Tick += (_, _) =>
    {
        try
        {
            ++ticks;
            if (ticks == 1)
            {
                // Drain creation/resize work first so the deltas below measure only
                // this callback's high-rate backing-surface commits.
                surface.Update();
                before = ParseSurfaceSnapshot(Snapshot());
                using var graphics = Graphics.FromHwnd(window);
                for (var frame = 0; frame < 16; ++frame)
                {
                    using var brush = new SolidBrush(Color.FromArgb(
                        255, 32 + frame * 8, 48 + frame * 3, 96 + frame * 4));
                    graphics.FillRectangle(brush, 0, 0, 96, 48);
                    graphics.Flush();
                }
                var leased = graphics.GetHdc();
                var leasedBrush = NativeSurfaceProbe.CreateSolidBrush(0x00664422u);
                var leasedArea = new NativeSurfaceProbe.NativeRect { Right = 24, Bottom = 16 };
                Require(leased != 0 && leasedBrush != 0 &&
                    NativeSurfaceProbe.FillRect(leased, ref leasedArea, leasedBrush) != 0,
                    "bitmap-backed HDC lease accepts direct GDI mutation");
                _ = NativeSurfaceProbe.DeleteObject(leasedBrush);
                graphics.ReleaseHdc(leased);
                var constructionBrush = NativeSurfaceProbe.CreateSolidBrush(0x00664422u);
                var constructionArea = new NativeSurfaceProbe.NativeRect
                {
                    Left = 24, Top = 0, Right = 96, Bottom = 48,
                };
                Require(constructionBrush != 0 &&
                    NativeSurfaceProbe.FillRect(constructionDevice, ref constructionArea,
                        constructionBrush) != 0,
                    "HDC retained from Handle construction remains writable after first show");
                _ = NativeSurfaceProbe.DeleteObject(constructionBrush);
                var device = NativeSurfaceProbe.GetDC(window);
                Require(device != 0, "control HWND exposes a device context");
                paintedCompatibilitySurface =
                    NativeSurfaceProbe.GetPixel(device, 20, 20) != 0xffffffffu &&
                    NativeSurfaceProbe.GetPixel(device, 48, 20) == 0x00664422u;
                _ = NativeSurfaceProbe.ReleaseDC(window, device);
                _ = NativeSurfaceProbe.ReleaseDC(window, constructionDevice);
                during = ParseSurfaceSnapshot(Snapshot());
                if (traceSurface) Console.Error.WriteLine("native-surface-snapshot=during|" + Snapshot());
                Require(before is not null && during["state"] == "live" &&
                    SurfaceMetric(during, "content") >= SurfaceMetric(before, "content") + 18 &&
                    SurfaceMetric(during, "published") <= SurfaceMetric(during, "content") &&
                    SurfaceMetric(during, "explicit") == 1,
                    "completed producer boundaries publish without an ordinary UI paint drain");
                return;
            }
            if (!newestReached)
            {
                after = ParseSurfaceSnapshot(Snapshot());
                if (traceSurface) Console.Error.WriteLine("native-surface-snapshot=after|" + Snapshot());
                Require(before is not null && during is not null &&
                    after["state"] == "live" &&
                    SurfaceMetric(after, "published") == SurfaceMetric(after, "content") &&
                    SurfaceMetric(after, "publish-requests") - SurfaceMetric(before, "publish-requests") >= 18 &&
                    SurfaceMetric(after, "publish-drops") == SurfaceMetric(before, "publish-drops") &&
                    SurfaceMetric(after, "explicit") == 1,
                    "the newest complete producer generation reaches the live slot by the next display turn");
                newestReached = true;
                return;
            }
            timer.Stop();
            form.Close();
        }
        catch (Exception error)
        {
            failure = error;
            timer.Stop();
            form.Close();
        }
    };
    timer.Start();
    TraceSurface("run");
    Application.Run(form);
    if (failure is not null) throw failure;
    Require(ticks >= 2, "native surface crosses an event-loop presentation boundary");
    Require(paintedCompatibilitySurface,
        "direct GDI targets the isolated compatibility HWND");
    using (var trace = global::System.Text.Json.JsonDocument.Parse(Application.LastHostTrace))
    {
        var live = trace.RootElement.GetProperty("host").GetProperty("live_presentations");
        if (traceSurface) Console.Error.WriteLine("native-surface-host=" + Application.LastHostTrace);
        Require(live.GetProperty("clock_signals").GetUInt64() >= 1 &&
            live.GetProperty("drains").GetUInt64() >= 1 &&
            live.GetProperty("updates_presented").GetUInt64() >= 1 &&
            live.GetProperty("updates_failed").GetUInt64() == 0,
            "the display clock samples and presents the live surface independently");
    }
    surface.Dispose();
    Require(!NativeSurfaceProbe.IsWindow(window), "control HWND is destroyed with its owner");
    Console.WriteLine("native-surface=handle:virtual|input:retained-host|size:96x48|gdi:true|producer:uncoupled|newest-frame:true|display-clock:true|disposed:true");
    form.Dispose();
    return 0;
}

static int RunNativeWindowSurfaceLifecycleHost()
{
    var traceLifecycle = Environment.GetEnvironmentVariable(
        "GUI_FORMS_TRACE_NATIVE_SURFACES") == "1";
    void TraceLifecycle(string value)
    {
        if (traceLifecycle) Console.Error.WriteLine(
            "native-surface-lifecycle-stage=" + value);
    }
    TraceLifecycle("construct");
    const int finalFrame = 2400;
    var form = new Form
    {
        Name = "nativeSurfaceLifecycleForm",
        Text = "Native surface lifecycle",
        Size = new Size(320, 210),
    };
    var surface = new PaintInputProbe
    {
        Name = "nativeSurfaceLifecycle",
        Bounds = new Rectangle(16, 16, 96, 48),
        BackColor = Color.Black,
    };
    var cover = new Panel
    {
        Name = "nativeSurfaceCover",
        Bounds = surface.Bounds,
        BackColor = Color.Magenta,
        Visible = false,
    };
    form.Controls.Add(surface);
    form.Controls.Add(cover);

    // This is the defining NativeBitmap behavior: acquire the control DC while
    // the managed control is still being constructed and retain it across the
    // entire hosted lifetime.
    var window = surface.Handle;
    TraceLifecycle("handle-acquired");
    var constructionDevice = NativeSurfaceProbe.GetDC(window);
    Require(window != 0 && constructionDevice != 0,
        "construction-retained HWND/HDC lease is available");
    Require(NativeSurfaceProbe.GetLayeredWindowAttributes(
            window, out _, out var endpointAlpha, out var endpointLayerFlags) &&
        endpointAlpha == 0 && (endpointLayerFlags & 0x00000002u) != 0 &&
        !NativeSurfaceProbe.IsWindowEnabled(window) &&
        NativeSurfaceProbe.GetParent(window) == 0,
        "compatibility paint endpoint has zero presentation alpha and no input or parent authority");

    var snapshotMethod = typeof(Control).GetMethod("__WindowSurfaceSnapshot",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("window-surface diagnostics are unavailable");
    Dictionary<string, string> Snapshot() => ParseSurfaceSnapshot(
        (string)(snapshotMethod.Invoke(surface, null) ?? string.Empty));

    static uint FrameColor(int frame) =>
        (uint)((frame * 37 + 11) & 0xff) |
        ((uint)((frame * 67 + 29) & 0xff) << 8) |
        ((uint)((frame * 97 + 53) & 0xff) << 16);
    var uiThread = Environment.CurrentManagedThreadId;
    var drawWidth = 96;
    var drawHeight = 48;
    var framesWritten = 0;
    var workerDone = 0;
    Exception? workerFailure = null;
    var producerClock = global::System.Diagnostics.Stopwatch.StartNew();
    TimeSpan producerElapsed = TimeSpan.Zero;
    var worker = new Thread(() =>
    {
        try
        {
            for (var frame = 0; frame <= finalFrame; ++frame)
            {
                var brush = NativeSurfaceProbe.CreateSolidBrush(FrameColor(frame));
                try
                {
                    var area = new NativeSurfaceProbe.NativeRect
                    {
                        Right = Volatile.Read(ref drawWidth),
                        Bottom = Volatile.Read(ref drawHeight),
                    };
                    if (brush == 0 || NativeSurfaceProbe.FillRect(
                        constructionDevice, ref area, brush) == 0)
                        throw new InvalidOperationException(
                            "background GDI frame write failed");
                }
                finally
                {
                    if (brush != 0) _ = NativeSurfaceProbe.DeleteObject(brush);
                }
                Volatile.Write(ref framesWritten, frame + 1);
                Thread.Sleep(1);
            }
        }
        catch (Exception error) { workerFailure = error; }
        finally
        {
            producerElapsed = producerClock.Elapsed;
            Volatile.Write(ref workerDone, 1);
        }
    }) { IsBackground = true, Name = "NativeBitmap compatibility writer" };

    var resized = false;
    var hidden = false;
    var shown = false;
    var covered = false;
    var revealed = false;
    var deadline = global::System.Diagnostics.Stopwatch.StartNew();
    Exception? uiFailure = null;
    var tracedDevicePixels = false;
    var uiTicks = 0;
    TimeSpan? workerCompletedAt = null;
    var captureHoldMilliseconds = int.TryParse(
        Environment.GetEnvironmentVariable("GUI_FORMS_LIVE_SURFACE_CAPTURE_HOLD_MS"),
        out var requestedCaptureHold)
        ? Math.Clamp(requestedCaptureHold, 0, 10_000) : 0;
    using var timer = new System.Windows.Forms.Timer { Interval = 10 };
    form.Load += (_, _) => worker.Start();
    form.Load += (_, _) => TraceLifecycle("form-load");
    timer.Tick += (_, _) =>
    {
        try
        {
            ++uiTicks;
            var completedFrames = Volatile.Read(ref framesWritten);
            if (!resized && completedFrames >= 240)
            {
                TraceLifecycle("resize-begin");
                surface.Size = new Size(128, 64);
                TraceLifecycle("resize-end");
                cover.Size = surface.Size;
                Volatile.Write(ref drawWidth, 128);
                Volatile.Write(ref drawHeight, 64);
                resized = true;
            }
            if (!hidden && completedFrames >= 600)
            {
                surface.Visible = false;
                hidden = true;
            }
            if (!shown && completedFrames >= 960)
            {
                surface.Visible = true;
                shown = true;
            }
            if (!covered && completedFrames >= 1320)
            {
                cover.Visible = true;
                cover.BringToFront();
                covered = true;
            }
            if (!revealed && completedFrames >= 1800)
            {
                cover.SendToBack();
                cover.Visible = false;
                surface.BringToFront();
                revealed = true;
            }

            if (Volatile.Read(ref workerDone) != 0)
            {
                if (workerFailure is not null) throw workerFailure;
                workerCompletedAt ??= deadline.Elapsed;
                // Leave several compositor opportunities after the producer's
                // final write. No producer notification or import is involved.
                if (deadline.Elapsed - workerCompletedAt.Value <
                    TimeSpan.FromMilliseconds(200 + captureHoldMilliseconds)) return;
                if (traceLifecycle && !tracedDevicePixels)
                {
                    tracedDevicePixels = true;
                    TraceLifecycle("dc-pixels=" +
                        NativeSurfaceProbe.GetPixel(constructionDevice, 1, 1).ToString("x8") + "," +
                        NativeSurfaceProbe.GetPixel(constructionDevice, 126, 62).ToString("x8"));
                }
                var state = Snapshot();
                if (traceLifecycle) TraceLifecycle("snapshot-" + string.Join(",",
                    state.Select(item => item.Key + "=" + item.Value)));
                Require(state["state"] == "live" &&
                    !state.ContainsKey("captured") &&
                    !state.ContainsKey("content-hash") &&
                    !state.ContainsKey("drains-started"),
                    "live endpoint has no observer, capture, hash, or drain state");
                Require(resized && hidden && shown && covered && revealed,
                    "resize, visibility, occlusion, and reveal stages completed");
                Require(uiTicks >= 20,
                    "UI dispatcher remained responsive during sustained producer writes");
                Require(NativeSurfaceProbe.GetClientRect(window, out var bounds) &&
                    bounds.Right - bounds.Left >= 128 &&
                    bounds.Bottom - bounds.Top >= 64,
                    "retained construction HDC capacity covers endpoint resize");
                Require(NativeSurfaceProbe.GetPixel(constructionDevice, 1, 1) ==
                        FrameColor(finalFrame) &&
                    NativeSurfaceProbe.GetPixel(constructionDevice, 126, 62) ==
                        FrameColor(finalFrame),
                    "final frame is uniform rather than an echoed partial update");
                Require(NativeSurfaceProbe.GetLayeredWindowAttributes(
                        window, out _, out var finalEndpointAlpha,
                        out var finalEndpointLayerFlags) &&
                    finalEndpointAlpha == 0 &&
                    (finalEndpointLayerFlags & 0x00000002u) != 0 &&
                    !NativeSurfaceProbe.IsWindowEnabled(window) &&
                    NativeSurfaceProbe.GetParent(window) == 0,
                    "paint endpoint never acquired presentation, input, or parent authority");
                Require(surface.PaintThreadIds.All(thread => thread == uiThread),
                    "background GDI writes never re-enter managed paint");
                timer.Stop();
                _ = NativeSurfaceProbe.ReleaseDC(window, constructionDevice);
                constructionDevice = 0;
                form.Close();
                return;
            }
            if (deadline.Elapsed > TimeSpan.FromSeconds(8))
                throw new TimeoutException(
                    "live NativeBitmap surface did not complete its sustained gate");
        }
        catch (Exception error)
        {
            uiFailure = error;
            if (traceLifecycle) Console.Error.WriteLine(
                "native-surface-lifecycle-failure=" + error);
            timer.Stop();
            form.Close();
        }
    };

    timer.Start();
    TraceLifecycle("run-begin");
    Application.Run(form);
    TraceLifecycle("run-end");
    Require(worker.Join(TimeSpan.FromSeconds(2)),
        "background NativeBitmap writer terminates");
    if (constructionDevice != 0)
        _ = NativeSurfaceProbe.ReleaseDC(window, constructionDevice);
    if (uiFailure is not null) throw uiFailure;
    if (workerFailure is not null) throw workerFailure;
    Require(producerElapsed < TimeSpan.FromSeconds(5),
        "producer was not serialized behind compositor/UI work");
    ulong displayClockSignals;
    ulong livePresentationDrains;
    ulong liveUpdatesPresented;
    ulong liveUpdatesFailed;
    ulong retainedFramesPresented;
    using (var trace = global::System.Text.Json.JsonDocument.Parse(
        Application.LastHostTrace))
    {
        if (traceLifecycle) Console.Error.WriteLine(
            "native-surface-lifecycle-host=" + Application.LastHostTrace);
        var metrics = trace.RootElement.GetProperty("window");
        retainedFramesPresented =
            metrics.GetProperty("frames_presented").GetUInt64();
        var live = trace.RootElement.GetProperty("host")
            .GetProperty("live_presentations");
        displayClockSignals = live.GetProperty("clock_signals").GetUInt64();
        livePresentationDrains = live.GetProperty("drains").GetUInt64();
        liveUpdatesPresented =
            live.GetProperty("updates_presented").GetUInt64();
        liveUpdatesFailed = live.GetProperty("updates_failed").GetUInt64();
        // Direct live surfaces deliberately bypass retained
        // invalidation. Judge sustained animation by the terminal host's
        // display clock and presentation lane, not by ordinary paint polls.
        Require(displayClockSignals >= 30 && livePresentationDrains >= 30 &&
            liveUpdatesPresented >= 30 && liveUpdatesFailed == 0,
            "live surface sustained compositor frames beyond one screen of history");
    }
    surface.Dispose();
    Require(!NativeSurfaceProbe.IsWindow(window),
        "disposing the retained control releases its compatibility endpoint");
    cover.Dispose();
    form.Dispose();
    Console.WriteLine(
        "native-surface-lifecycle=construction-hdc:retained|writer:background|producer:uncoupled|compositor:continuous|resize:durable|hide-show:latest|occlusion:latest|echo:none|reentry:none|secondary-window:none|disposed:true");
    Console.WriteLine(
        $"native-surface-performance=producer-frames:{finalFrame + 1}|producer-ms:{producerElapsed.TotalMilliseconds:F0}|ui-ticks:{uiTicks}|display-clock-signals:{displayClockSignals}|live-drains:{livePresentationDrains}|live-updates-presented:{liveUpdatesPresented}|live-updates-failed:{liveUpdatesFailed}|retained-frames-presented:{retainedFramesPresented}");
    return 0;
}

static int RunNativeWindowSurfaceFallbackHost()
{
    var form = new Form { Name = "nativeSurfaceFallbackForm", Text = "Native surface fallback", Size = new Size(240, 140) };
    var surface = new PaintInputProbe
    {
        Name = "nativeSurfaceFallback",
        Bounds = new Rectangle(12, 12, 96, 48),
        BackColor = Color.Black,
    };
    form.Controls.Add(surface);
    var window = surface.Handle;
    var snapshotMethod = typeof(Control).GetMethod("__WindowSurfaceSnapshot",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("window-surface diagnostics are unavailable");
    Dictionary<string, string> Snapshot() => ParseSurfaceSnapshot(
        (string)(snapshotMethod.Invoke(surface, null) ?? string.Empty));

    using var timer = new System.Windows.Forms.Timer { Interval = 300 };
    Dictionary<string, string>? before = null;
    var ticks = 0;
    Exception? failure = null;
    timer.Tick += (_, _) =>
    {
        try
        {
            ++ticks;
            if (ticks == 1)
            {
                surface.Update();
                before = Snapshot();
                var device = NativeSurfaceProbe.GetDC(window);
                Require(device != 0, "compatibility surface exposes a device context");
                var brush = NativeSurfaceProbe.CreateSolidBrush(0x003322ccu);
                var area = new NativeSurfaceProbe.NativeRect { Right = 96, Bottom = 48 };
                Require(brush != 0 && NativeSurfaceProbe.FillRect(device, ref area, brush) != 0,
                    "raw GDI fill succeeds through the compatibility endpoint");
                _ = NativeSurfaceProbe.DeleteObject(brush);
                _ = NativeSurfaceProbe.ReleaseDC(window, device);
                return;
            }
            var after = Snapshot();
            Require(before is not null && after["state"] == "live" &&
                SurfaceMetric(after, "content") >= SurfaceMetric(before, "content") + 1 &&
                SurfaceMetric(after, "published") == SurfaceMetric(after, "content") &&
                SurfaceMetric(after, "publish-requests") -
                    SurfaceMetric(before, "publish-requests") == 1 &&
                SurfaceMetric(after, "explicit") == 0,
                "the intercepted GDI boundary publishes one newest-frame update");
            timer.Stop();
            form.Close();
        }
        catch (Exception error)
        {
            failure = error;
            timer.Stop();
            form.Close();
        }
    };
    timer.Start();
    Application.Run(form);
    if (failure is not null) throw failure;
    Require(ticks >= 2, "fallback probe crosses a bounded event-loop boundary");
    surface.Dispose();
    form.Dispose();
    Console.WriteLine("native-surface-fallback=raw-gdi:true|shim-boundary:true|newest-frame:true|disposed:true");
    return 0;
}

static int RunPaintReentryHost()
{
    var form = new Form { Name = "paintReentryForm", Text = "Paint reentry", Size = new Size(240, 140) };
    var probe = new ReentrantPaintProbe { Name = "paintReentryProbe", Bounds = new Rectangle(12, 12, 96, 48) };
    form.Controls.Add(probe);
    var before = 0;
    form.Load += (_, _) =>
    {
        form.BeginInvoke((Action)(() =>
        {
            before = probe.Paints;
            probe.RequestUpdateDuringNextPaint = true;
            probe.Refresh();
            Require(probe.Paints == before + 1 && probe.MaximumDepth == 1,
                "Update during OnPaint defers without recursive application paint");
            form.BeginInvoke((Action)(() =>
            {
                Require(probe.Paints == before + 2 && probe.MaximumDepth == 1,
                    "one deferred managed-paint follow-up drains after callback return");
                form.Close();
            }));
        }));
    };
    Application.Run(form);
    Console.WriteLine("paint-reentry=recursive:false|follow-up:one|callback-boundary:true");
    form.Dispose();
    return 0;
}

static int RunManagedDoubleBufferHost()
{
    var form = new Form
    {
        Name = "managedDoubleBufferForm",
        Text = "Managed double buffer",
        Size = new Size(280, 170),
    };
    var probe = new ManagedDoubleBufferProbe
    {
        Name = "managedDoubleBufferProbe",
        Bounds = new Rectangle(12, 12, 96, 48),
        BackColor = Color.FromArgb(31, 48, 66),
    };
    probe.Buffered = true;
    form.Controls.Add(probe);

    var snapshotMethod = typeof(Control).GetMethod("__ManagedPaintSurfaceSnapshot",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("managed paint diagnostics are unavailable");
    Dictionary<string, string> Snapshot() => ParseSurfaceSnapshot(
        (string)(snapshotMethod.Invoke(probe, null) ?? string.Empty));

    form.Load += (_, _) =>
    {
        form.BeginInvoke((Action)(() =>
        {
            var before = Snapshot();
            Require(probe.Buffered && probe.OptimizedBuffering && probe.AllPainting,
                "DoubleBuffered is readable and reflected in compatible style state");
            Require(before["double-buffered"] == "1" && before["surface"] == "1" &&
                before["size"] == "96x48",
                "double-buffered owner paint owns one size-matched private surface");
            var paints = probe.Paints;
            for (var revision = 0; revision < 16; ++revision) probe.Invalidate();
            Require(probe.Paints == paints,
                "a mutation burst does not paint before its callback boundary");
            form.BeginInvoke((Action)(() =>
            {
                var coalesced = Snapshot();
                Require(probe.Paints == paints + 1 &&
                    SurfaceMetric(coalesced, "allocations") == SurfaceMetric(before, "allocations") &&
                    SurfaceMetric(coalesced, "reuses") > SurfaceMetric(before, "reuses") &&
                    SurfaceMetric(coalesced, "queue-coalesced") -
                        SurfaceMetric(before, "queue-coalesced") >= 15,
                    "sixteen invalidations reuse one surface and drain as one paint");

                var beforeResizePaints = probe.Paints;
                var beforeResize = coalesced;
                probe.ResizeDuringNextPaint = true;
                probe.Refresh();
                var stale = Snapshot();
                Require(probe.Paints == beforeResizePaints + 1 &&
                    probe.MaximumDepth == 1 && stale["state"] == "dirty_queued" &&
                    SurfaceMetric(stale, "leases-abandoned") ==
                        SurfaceMetric(beforeResize, "leases-abandoned") + 1 &&
                    SurfaceMetric(stale, "epoch") > SurfaceMetric(beforeResize, "epoch"),
                    "resize during owner paint abandons the stale epoch without recursion");

                form.BeginInvoke((Action)(() =>
                {
                    var resized = Snapshot();
                    Require(probe.Paints == beforeResizePaints + 2 &&
                        resized["state"] == "clean" && resized["size"] == "128x64" &&
                        SurfaceMetric(resized, "content") == SurfaceMetric(resized, "rendered") &&
                        probe.Backgrounds == probe.Paints && probe.SharedGraphics,
                        "one deferred pass paints background and foreground into the replacement surface");

                    var allocations = SurfaceMetric(resized, "allocations");
                    probe.Buffered = false;
                    probe.Refresh();
                    var unbuffered = Snapshot();
                    Require(!probe.Buffered && !probe.OptimizedBuffering &&
                        unbuffered["double-buffered"] == "0" &&
                        unbuffered["surface"] == "0" &&
                        SurfaceMetric(unbuffered, "allocations") == allocations + 1,
                        "disabling the compatibility request retires persistence but keeps coherent ephemeral paint");
                    var beforeFaultPaints = probe.Paints;
                    var beforeFault = unbuffered;
                    probe.ThrowDuringNextPaint = true;
                    probe.Refresh();
                    var faulted = Snapshot();
                    Require(probe.Paints == beforeFaultPaints + 1 &&
                        faulted["queued"] == "0" &&
                        SurfaceMetric(faulted, "leases-abandoned") ==
                            SurfaceMetric(beforeFault, "leases-abandoned") + 1,
                        "a callback fault abandons its candidate without queuing a self-retry");
                    form.BeginInvoke((Action)(() =>
                    {
                        var settledFault = Snapshot();
                        Require(probe.Paints == beforeFaultPaints + 1 &&
                            settledFault["queued"] == "0",
                            "a callback fault remains quiescent until a real later touch");
                        form.Close();
                    }));
                }));
            }));
        }));
    };
    Application.Run(form);
    Require(Application.LastCallbackException is InvalidOperationException,
        "the deliberate managed owner-paint failure is reported exactly once");
    probe.Dispose();
    form.Dispose();
    Console.WriteLine("managed-double-buffer=reflected:true|phases:shared|surface:reused|burst:coalesced|resize:stale-abandoned|follow-up:one|disabled:ephemeral|fault:no-self-retry");
    return 0;
}

static int RunManagedDamageHost()
{
    var form = new Form
    {
        Name = "managedDamageForm",
        Text = "Managed damage",
        Size = new Size(320, 190),
    };
    var parent = new ManagedDoubleBufferProbe
    {
        Name = "managedDamageParent",
        Bounds = new Rectangle(12, 12, 96, 48),
        BackColor = Color.FromArgb(31, 48, 66),
    };
    var child = new ManagedDoubleBufferProbe
    {
        Name = "managedDamageChild",
        Bounds = new Rectangle(20, 10, 30, 20),
        BackColor = Color.FromArgb(42, 62, 81),
    };
    parent.Buffered = true;
    child.Buffered = true;
    parent.Controls.Add(child);
    form.Controls.Add(parent);

    var parentEvents = new List<Rectangle>();
    var childEvents = new List<Rectangle>();
    parent.Invalidated += (_, e) => parentEvents.Add(e.InvalidRect);
    child.Invalidated += (_, e) => childEvents.Add(e.InvalidRect);
    var snapshotMethod = typeof(Control).GetMethod("__ManagedPaintSurfaceSnapshot",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("managed paint diagnostics are unavailable");
    Dictionary<string, string> Snapshot(Control control) => ParseSurfaceSnapshot(
        (string)(snapshotMethod.Invoke(control, null) ?? string.Empty));

    form.Load += (_, _) =>
    {
        form.BeginInvoke((Action)(() =>
        {
            parent.Clips.Clear();
            child.Clips.Clear();
            parentEvents.Clear();
            childEvents.Clear();
            parent.InvalidatedHooks.Clear();
            child.InvalidatedHooks.Clear();
            var notifyPaints = parent.Paints;
            var notifyRevision = SurfaceMetric(Snapshot(parent), "content");
            parent.NotifyOnly(new Rectangle(1, 2, 3, 4));
            Require(parentEvents.SequenceEqual(new[] { new Rectangle(1, 2, 3, 4) }) &&
                parent.InvalidatedHooks.SequenceEqual(parentEvents) &&
                parent.Paints == notifyPaints &&
                SurfaceMetric(Snapshot(parent), "content") == notifyRevision,
                "NotifyInvalidate raises the protected hook and public event without scheduling paint");
            parentEvents.Clear();
            parent.InvalidatedHooks.Clear();
            var parentPaints = parent.Paints;
            var before = Snapshot(parent);
            parent.Invalidate(new Rectangle(5, 6, 10, 8));
            parent.Invalidate(new Rectangle(12, 10, 10, 12));
            var pending = Snapshot(parent);
            Require(parent.Paints == parentPaints &&
                parentEvents.SequenceEqual(new[]
                {
                    new Rectangle(5, 6, 10, 8),
                    new Rectangle(12, 10, 10, 12),
                }) &&
                parent.InvalidatedHooks.SequenceEqual(parentEvents) &&
                pending["damage"] == "5,6,17,16" &&
                SurfaceMetric(pending, "partial-touches") ==
                    SurfaceMetric(before, "partial-touches") + 2 &&
                SurfaceMetric(pending, "damage-merges") ==
                    SurfaceMetric(before, "damage-merges") + 1,
                "rectangle invalidations publish synchronously and merge before paint");

            form.BeginInvoke((Action)(() =>
            {
                var rendered = Snapshot(parent);
                Require(parent.Paints == parentPaints + 1 &&
                    parent.Clips.Last() == new Rectangle(5, 6, 17, 16) &&
                    rendered["last-damage"] == "5,6,17,16" &&
                    rendered["damage"] == "0,0,0,0",
                    "one persistent owner-paint lease consumes the merged damage clip");

                parentEvents.Clear();
                parent.Clips.Clear();
                using (var region = new Region(new Rectangle(30, 4, 12, 9)))
                {
                    Require(region.GetBounds(null!) == new RectangleF(30, 4, 12, 9),
                        "GUI.Drawing Region bounds project through the native retained region");
                    parent.Invalidate(region);
                    parent.Update();
                }
                Require(parentEvents.SequenceEqual(new[] { new Rectangle(30, 4, 12, 9) }) &&
                    parent.Clips.Last() == new Rectangle(30, 4, 12, 9),
                    "region invalidation uses an outward bounded rectangle");

                parentEvents.Clear();
                childEvents.Clear();
                parent.Clips.Clear();
                child.Clips.Clear();
                parent.Invalidate(new Rectangle(10, 5, 30, 20), true);
                parent.Update();
                child.Update();
                Require(parentEvents.SequenceEqual(new[] { new Rectangle(10, 5, 30, 20) }) &&
                    childEvents.SequenceEqual(new[] { new Rectangle(0, 0, 20, 15) }) &&
                    parent.Clips.Last() == new Rectangle(10, 5, 30, 20) &&
                    child.Clips.Last() == new Rectangle(0, 0, 20, 15),
                    "child propagation intersects parent damage and translates to child coordinates");

                parentEvents.Clear();
                parent.Clips.Clear();
                var beforeFault = Snapshot(parent);
                parent.ThrowDuringNextPaint = true;
                parent.Invalidate(new Rectangle(7, 8, 9, 10));
                parent.Update();
                var faulted = Snapshot(parent);
                Require(faulted["damage"] == "7,8,9,10" && faulted["queued"] == "0" &&
                    SurfaceMetric(faulted, "leases-abandoned") ==
                        SurfaceMetric(beforeFault, "leases-abandoned") + 1,
                    "a failed partial lease restores its consumed damage without self-retry");
                parent.Invalidate(new Rectangle(20, 20, 2, 2));
                parent.Update();
                Require(parent.Clips.Last() == new Rectangle(7, 8, 15, 14),
                    "the next real touch merges with damage restored from a failed lease");

                parent.Buffered = false;
                parent.Update();
                parent.Clips.Clear();
                parent.Invalidate(new Rectangle(4, 5, 6, 7));
                parent.Update();
                Require(parent.Clips.Last() == new Rectangle(0, 0, 96, 48),
                    "ephemeral owner paint promotes partial damage to a coherent full surface");
                form.Close();
            }));
        }));
    };
    Application.Run(form);
    Require(Application.LastCallbackException is InvalidOperationException,
        "the deliberate partial-paint fault is reported once");
    child.Dispose();
    parent.Dispose();
    form.Dispose();
    Console.WriteLine("managed-damage=rect:merged|notify:signal-only|event:clipped|region:bounded|children:translated|fault:restored|unbuffered:full");
    return 0;
}

static int RunPaintInputDeferralHost()
{
    var form = new Form
    {
        Name = "paintInputDeferralForm",
        Text = "Paint input deferral",
        Size = new Size(300, 180),
    };
    var probe = new DeferredInputPaintProbe
    {
        Name = "paintInputDeferralProbe",
        Bounds = new Rectangle(12, 12, 96, 48),
    };
    var disposedProbe = new DeferredInputPaintProbe
    {
        Name = "paintInputDisposedProbe",
        Bounds = new Rectangle(12, 72, 96, 48),
    };
    probe.Buffered = true;
    disposedProbe.Buffered = true;
    form.Controls.Add(probe);
    form.Controls.Add(disposedProbe);
    var snapshotMethod = typeof(Control).GetMethod("__ManagedPaintSurfaceSnapshot",
        global::System.Reflection.BindingFlags.Instance |
        global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("managed paint diagnostics are unavailable");
    Dictionary<string, string> Snapshot(Control control) => ParseSurfaceSnapshot(
        (string)(snapshotMethod.Invoke(control, null) ?? string.Empty));

    form.Load += (_, _) =>
    {
        form.BeginInvoke((Action)(() =>
        {
            probe.InputEvents.Clear();
            probe.Clips.Clear();
            var paints = probe.Paints;
            var before = Snapshot(probe);
            probe.InjectInputDuringNextPaint = true;
            probe.Refresh();
            var deferred = Snapshot(probe);
            Require(probe.Paints == paints + 1 && probe.MaximumDepth == 1 &&
                probe.InputEvents.Count == 0 && deferred["input-pending"] == "2" &&
                SurfaceMetric(deferred, "inputs-deferred") ==
                    SurfaceMetric(before, "inputs-deferred") + 2 &&
                SurfaceMetric(deferred, "inputs-drained") ==
                    SurfaceMetric(before, "inputs-drained"),
                "pointer and key ingress remain queued until the paint lease returns");

            form.BeginInvoke((Action)(() =>
            {
                var drained = Snapshot(probe);
                Require(probe.InputEvents.SequenceEqual(new[] { "pointer", "key" }) &&
                    !probe.EventObservedDuringPaint && drained["input-pending"] == "0" &&
                    SurfaceMetric(drained, "inputs-drained") ==
                        SurfaceMetric(before, "inputs-drained") + 2 &&
                    probe.Paints == paints + 2,
                    "deferred input preserves order, runs outside application paint, and " +
                    "flushes one ordinary invalidation after its callback boundary");

                var disposedBefore = Snapshot(disposedProbe);
                disposedProbe.InjectInputDuringNextPaint = true;
                disposedProbe.DisposeDuringNextPaint = true;
                disposedProbe.Refresh();
                var disposed = Snapshot(disposedProbe);
                Require(disposedProbe.InputEvents.Count == 0 && disposed["input-pending"] == "0" &&
                    SurfaceMetric(disposed, "inputs-abandoned") ==
                        SurfaceMetric(disposedBefore, "inputs-abandoned") + 2,
                    "disposing a paint owner abandons queued input without callbacks");

                form.BeginInvoke((Action)(() =>
                {
                    var followed = Snapshot(probe);
                    Require(probe.Paints == paints + 2 && probe.MaximumDepth == 1 &&
                        probe.Clips.Last() == new Rectangle(2, 2, 4, 4) &&
                        followed["input-pending"] == "0",
                        "input mutation schedules one ordinary post-input paint without reentry");
                    form.Close();
                }));
            }));
        }));
    };
    Application.Run(form);
    Require(Application.LastCallbackException is null,
        "paint input deferral completes without callback failure");
    probe.Dispose();
    form.Dispose();
    Console.WriteLine("paint-input-deferral=order:pointer>key|during-paint:false|follow-up:one|disposed:abandoned|queue:zero");
    return 0;
}

static Dictionary<string, string> ParseSurfaceSnapshot(string snapshot)
{
    var result = new Dictionary<string, string>(StringComparer.Ordinal);
    foreach (var field in snapshot.Split('|', StringSplitOptions.RemoveEmptyEntries))
    {
        var separator = field.IndexOf(':');
        if (separator > 0) result[field[..separator]] = field[(separator + 1)..];
    }
    return result;
}

static long SurfaceMetric(Dictionary<string, string> snapshot, string key) =>
    long.Parse(snapshot[key], global::System.Globalization.CultureInfo.InvariantCulture);

static void Require(bool condition, string name)
{
    if (!condition) throw new InvalidOperationException($"M11d behavior check failed: {name}");
}

enum ReceiverMode { Slow, Fast, Precise }

sealed class ReceiverLevel
{
    internal ReceiverLevel(int value) { Value = value; }
    internal int Value { get; }
}

sealed class ReceiverLevelConverter : global::System.ComponentModel.TypeConverter
{
    internal static int FormatCalls { get; private set; }
    internal static string? LastCultureName { get; private set; }
    public override bool CanConvertFrom(global::System.ComponentModel.ITypeDescriptorContext? context,
        Type sourceType) => sourceType == typeof(string) || base.CanConvertFrom(context, sourceType);
    public override bool CanConvertTo(global::System.ComponentModel.ITypeDescriptorContext? context,
        Type? destinationType) => destinationType == typeof(string) || base.CanConvertTo(context, destinationType);
    public override object? ConvertFrom(global::System.ComponentModel.ITypeDescriptorContext? context,
        global::System.Globalization.CultureInfo? culture, object value) =>
        value is string text ? new ReceiverLevel(int.Parse(text, culture ?? global::System.Globalization.CultureInfo.CurrentCulture)) :
        base.ConvertFrom(context, culture, value);
    public override object? ConvertTo(global::System.ComponentModel.ITypeDescriptorContext? context,
        global::System.Globalization.CultureInfo? culture, object? value, Type destinationType)
    {
        if (destinationType == typeof(string) && value is ReceiverLevel level)
        {
            ++FormatCalls;
            LastCultureName = (culture ?? global::System.Globalization.CultureInfo.CurrentCulture).Name;
            return level.Value.ToString(culture ?? global::System.Globalization.CultureInfo.CurrentCulture);
        }
        return base.ConvertTo(context, culture, value, destinationType);
    }
    public override bool GetStandardValuesSupported(
        global::System.ComponentModel.ITypeDescriptorContext? context) => true;
    public override bool GetStandardValuesExclusive(
        global::System.ComponentModel.ITypeDescriptorContext? context) => true;
    public override StandardValuesCollection GetStandardValues(
        global::System.ComponentModel.ITypeDescriptorContext? context) =>
        new(new object[] { new ReceiverLevel(1), new ReceiverLevel(2) });
}

sealed class ManagedPropertyFixture
{
    private int? gain;
    private ReceiverMode mode;
    private ReceiverLevel level = new(1);
    private int rejectValue;
    internal int GetterCalls { get; private set; }

    [global::System.ComponentModel.DefaultValue(null)]
    public int? Gain { get { ++GetterCalls; return gain; } set => gain = value; }
    [global::System.ComponentModel.Editor(typeof(ReceiverModeEditor), typeof(global::System.Drawing.Design.UITypeEditor))]
    public ReceiverMode Mode { get { ++GetterCalls; return mode; } set => mode = value; }
    [global::System.ComponentModel.TypeConverter(typeof(ReceiverLevelConverter))]
    public ReceiverLevel Level { get { ++GetterCalls; return level; } set => level = value; }
    [global::System.ComponentModel.Editor(typeof(ReceiverValueEditor), typeof(global::System.Drawing.Design.UITypeEditor))]
    public int RejectValue { get { ++GetterCalls; return rejectValue; } set { if (RejectNine && value == 9) throw new InvalidOperationException("fixture rejects nine"); rejectValue = value; } }
    public string ReadOnlyStatus { get { ++GetterCalls; return "Ready"; } }
    [global::System.ComponentModel.Browsable(false)]
    public bool RejectNine { get; set; }
}

sealed class ReceiverModeEditor : global::System.Drawing.Design.UITypeEditor
{
    internal static int DropDownCalls { get; private set; }
    internal static int CloseCalls { get; private set; }
    public override global::System.Drawing.Design.UITypeEditorEditStyle GetEditStyle(
        global::System.ComponentModel.ITypeDescriptorContext? context) =>
        global::System.Drawing.Design.UITypeEditorEditStyle.DropDown;
    public override object EditValue(
        global::System.ComponentModel.ITypeDescriptorContext? context,
        global::System.IServiceProvider provider, object? value)
    {
        var service = provider.GetService(typeof(global::System.Windows.Forms.Design.IWindowsFormsEditorService))
            as global::System.Windows.Forms.Design.IWindowsFormsEditorService ??
            throw new InvalidOperationException("Property editor service was not projected.");
        using var choices = new ListBox { Name = "receiverModeChoices", Size = new Size(220, 96) };
        choices.Items.Add(ReceiverMode.Slow);
        choices.Items.Add(ReceiverMode.Fast);
        choices.Items.Add(ReceiverMode.Precise);
        var selected = value is ReceiverMode current ? current : ReceiverMode.Slow;
        choices.ParentChanged += (_, _) =>
        {
            if (choices.Parent is null) return;
            choices.BeginInvoke((Action)(() =>
            {
                selected = ReceiverMode.Precise;
                ++CloseCalls;
                service.CloseDropDown();
            }));
        };
        ++DropDownCalls;
        service.DropDownControl(choices);
        return selected;
    }
}

sealed class ReceiverValueEditor : global::System.Drawing.Design.UITypeEditor
{
    internal static int ModalCalls { get; private set; }
    internal static int AcceptedCalls { get; private set; }
    public override global::System.Drawing.Design.UITypeEditorEditStyle GetEditStyle(
        global::System.ComponentModel.ITypeDescriptorContext? context) =>
        global::System.Drawing.Design.UITypeEditorEditStyle.Modal;
    public override object EditValue(
        global::System.ComponentModel.ITypeDescriptorContext? context,
        global::System.IServiceProvider provider, object? value)
    {
        var service = provider.GetService(typeof(global::System.Windows.Forms.Design.IWindowsFormsEditorService))
            as global::System.Windows.Forms.Design.IWindowsFormsEditorService ??
            throw new InvalidOperationException("Modal property editor service was not projected.");
        using var dialog = new Form { Name = "receiverValueDialog", Text = "Receiver value", ClientSize = new Size(240, 96), FormBorderStyle = FormBorderStyle.FixedDialog, ShowInTaskbar = false };
        dialog.Load += (_, _) => dialog.BeginInvoke((Action)(() => dialog.DialogResult = DialogResult.OK));
        ++ModalCalls;
        if (service.ShowDialog(dialog) != DialogResult.OK) return value!;
        ++AcceptedCalls;
        return 4;
    }
}

sealed class UnsupportedPropertyFixture
{
    public UnsupportedProperty Value { get; set; } = new();
}

sealed class UnsupportedProperty { }

sealed class LoadProbe : UserControl
{
    internal int Loads { get; private set; }
    protected override void OnLoad(EventArgs e)
    {
        ++Loads;
        base.OnLoad(e);
    }
}

sealed class ConstructionReentryProbe : UserControl
{
    private bool constructionComplete;

    internal ConstructionReentryProbe()
    {
        constructionComplete = true;
    }

    internal int EarlyResizeCalls { get; private set; }
    internal int EarlyLayoutCalls { get; private set; }
    internal int ResizeCalls { get; private set; }
    internal int LayoutCalls { get; private set; }

    protected override void OnResize(EventArgs e)
    {
        if (constructionComplete) ++ResizeCalls;
        else ++EarlyResizeCalls;
        base.OnResize(e);
    }

    protected override void OnLayout(LayoutEventArgs e)
    {
        if (constructionComplete) ++LayoutCalls;
        else ++EarlyLayoutCalls;
        base.OnLayout(e);
    }
}

sealed class LayoutProbe : Panel
{
    internal int Layouts { get; private set; }
    protected override void OnLayout(LayoutEventArgs e)
    {
        ++Layouts;
        base.OnLayout(e);
    }
}

sealed class TransactionLayoutProbe : Panel
{
    internal int Layouts { get; private set; }
    internal int Reentries { get; set; }
    internal bool ThrowNext { get; set; }
    internal Control? FirstAffectedControl { get; private set; }
    internal string? FirstAffectedProperty { get; private set; }

    internal void Reset()
    {
        Layouts = 0;
        FirstAffectedControl = null;
        FirstAffectedProperty = null;
    }

    protected override void OnLayout(LayoutEventArgs e)
    {
        ++Layouts;
        FirstAffectedControl ??= e.AffectedControl;
        FirstAffectedProperty ??= e.AffectedProperty;
        if (ThrowNext)
        {
            ThrowNext = false;
            throw new InvalidOperationException("intentional layout fault");
        }
        base.OnLayout(e);
        if (Reentries > 0)
        {
            --Reentries;
            PerformLayout(this, "Reentrant");
        }
    }
}

sealed class DialogKeyProbe : Form
{
    internal bool Route(Keys keys) => ProcessDialogKey(keys);
}

sealed class StateChangeProbe : Control
{
    internal int TextChanges { get; private set; }
    internal int EnabledChanges { get; private set; }
    internal int VisibleChanges { get; private set; }
    internal int ForeColorChanges { get; private set; }
    internal int BackColorChanges { get; private set; }
    internal int FontChanges { get; private set; }

    protected override void OnTextChanged(EventArgs e) { ++TextChanges; base.OnTextChanged(e); }
    protected override void OnEnabledChanged(EventArgs e) { ++EnabledChanges; base.OnEnabledChanged(e); }
    protected override void OnVisibleChanged(EventArgs e) { ++VisibleChanges; base.OnVisibleChanged(e); }
    protected override void OnForeColorChanged(EventArgs e) { ++ForeColorChanges; base.OnForeColorChanged(e); }
    protected override void OnBackColorChanged(EventArgs e) { ++BackColorChanges; base.OnBackColorChanged(e); }
    protected override void OnFontChanged(EventArgs e) { ++FontChanges; base.OnFontChanged(e); }
}

sealed class MenuProbe : ContextMenuStrip
{
    internal void ReleaseAt(int x, int y) =>
        OnMouseUp(new MouseEventArgs(MouseButtons.Left, 1, x, y, 0));

    internal void MoveAt(int x, int y) =>
        OnMouseMove(new MouseEventArgs(MouseButtons.None, 0, x, y, 0));

    internal void Key(Keys key) => OnKeyDown(new KeyEventArgs(key));

    internal Bitmap RenderToBitmap()
    {
        var bitmap = new Bitmap(Width, Height);
        using var graphics = Graphics.FromImage(bitmap);
        var paintArgs = (PaintEventArgs)global::System.Activator.CreateInstance(
            typeof(PaintEventArgs),
            global::System.Reflection.BindingFlags.Instance |
                global::System.Reflection.BindingFlags.Public |
                global::System.Reflection.BindingFlags.NonPublic,
            null, [graphics, ClientRectangle], null)!;
        OnPaint(paintArgs);
        return bitmap;
    }
}

sealed class ComboProbe : ComboBox
{
    internal void ReleaseAt(int x, int y) =>
        OnMouseUp(new MouseEventArgs(MouseButtons.Left, 1, x, y, 0));
    internal void DragSelect(int startX, int endX)
    {
        OnMouseDown(new MouseEventArgs(MouseButtons.Left, 1, startX, Height / 2, 0));
        OnMouseMove(new MouseEventArgs(MouseButtons.Left, 0, endX, Height / 2, 0));
        OnMouseUp(new MouseEventArgs(MouseButtons.Left, 1, endX, Height / 2, 0));
    }
}

sealed class TextProbe : TextBox
{
    internal void DragSelect(int startX, int endX)
    {
        OnMouseDown(new MouseEventArgs(MouseButtons.Left, 1, startX, Height / 2, 0));
        OnMouseMove(new MouseEventArgs(MouseButtons.Left, 0, endX, Height / 2, 0));
        OnMouseUp(new MouseEventArgs(MouseButtons.Left, 1, endX, Height / 2, 0));
    }
}

sealed class PaintInputProbe : Control
{
    internal int Paints { get; private set; }
    internal List<int> PaintThreadIds { get; } = new();
    internal int MouseDowns { get; private set; }
    internal int MouseUps { get; private set; }
    internal Point LastPoint { get; private set; }

    protected override void OnPaint(PaintEventArgs e)
    {
        ++Paints;
        PaintThreadIds.Add(Environment.CurrentManagedThreadId);
        base.OnPaint(e);
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        ++MouseDowns;
        LastPoint = new Point(e.X, e.Y);
        base.OnMouseDown(e);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        ++MouseUps;
        LastPoint = new Point(e.X, e.Y);
        base.OnMouseUp(e);
    }
}

sealed class ReentrantPaintProbe : Control
{
    internal int Paints { get; private set; }
    internal int MaximumDepth { get; private set; }
    internal bool RequestUpdateDuringNextPaint { get; set; }
    private int depth;

    protected override void OnPaint(PaintEventArgs e)
    {
        ++depth;
        MaximumDepth = Math.Max(MaximumDepth, depth);
        ++Paints;
        try
        {
            if (RequestUpdateDuringNextPaint)
            {
                RequestUpdateDuringNextPaint = false;
                Update();
            }
            base.OnPaint(e);
        }
        finally { --depth; }
    }
}

sealed class ManagedDoubleBufferProbe : Control
{
    internal bool Buffered { get => DoubleBuffered; set => DoubleBuffered = value; }
    internal bool OptimizedBuffering => GetStyle(ControlStyles.OptimizedDoubleBuffer);
    internal bool AllPainting => GetStyle(ControlStyles.AllPaintingInWmPaint);
    internal int Paints { get; private set; }
    internal int Backgrounds { get; private set; }
    internal int MaximumDepth { get; private set; }
    internal bool SharedGraphics { get; private set; } = true;
    internal bool ResizeDuringNextPaint { get; set; }
    internal bool ThrowDuringNextPaint { get; set; }
    internal List<Rectangle> Clips { get; } = new();
    internal List<Rectangle> InvalidatedHooks { get; } = new();
    internal void NotifyOnly(Rectangle damage) => NotifyInvalidate(damage);
    private Graphics? backgroundGraphics;
    private int depth;

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        ++Backgrounds;
        backgroundGraphics = e.Graphics;
        base.OnPaintBackground(e);
    }

    protected override void OnInvalidated(InvalidateEventArgs e)
    {
        InvalidatedHooks.Add(e.InvalidRect);
        base.OnInvalidated(e);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        ++depth;
        MaximumDepth = Math.Max(MaximumDepth, depth);
        ++Paints;
        Clips.Add(e.ClipRectangle);
        SharedGraphics &= ReferenceEquals(backgroundGraphics, e.Graphics);
        try
        {
            using var brush = new SolidBrush(Color.FromArgb(80, 162, 220));
            e.Graphics.FillRectangle(brush, 4, 4,
                Math.Max(1, Width - 8), Math.Max(1, Height - 8));
            if (ResizeDuringNextPaint)
            {
                ResizeDuringNextPaint = false;
                Size = new Size(128, 64);
            }
            if (ThrowDuringNextPaint)
            {
                ThrowDuringNextPaint = false;
                throw new InvalidOperationException("managed owner-paint fault probe");
            }
            base.OnPaint(e);
        }
        finally { --depth; }
    }
}

sealed class BackgroundOnlyPanelProbe : Panel
{
    internal int Backgrounds { get; private set; }
    internal Color LastPaintedColor { get; private set; }

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        ++Backgrounds;
        base.OnPaintBackground(e);
        LastPaintedColor = Color.FromArgb(25, 25, 25);
        using var surface = new SolidBrush(LastPaintedColor);
        e.Graphics.FillRectangle(surface, ClientRectangle);
    }
}

sealed class DeferredInputPaintProbe : Control
{
    private static readonly global::System.Reflection.MethodInfo InjectPointer =
        typeof(Control).GetMethod("__InjectManagedPointer",
            global::System.Reflection.BindingFlags.Instance |
            global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("managed pointer injection is unavailable");
    private static readonly global::System.Reflection.MethodInfo InjectKey =
        typeof(Control).GetMethod("__InjectManagedKey",
            global::System.Reflection.BindingFlags.Instance |
            global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("managed key injection is unavailable");
    private int depth;

    internal bool Buffered { get => DoubleBuffered; set => DoubleBuffered = value; }
    internal bool InjectInputDuringNextPaint { get; set; }
    internal bool DisposeDuringNextPaint { get; set; }
    internal bool EventObservedDuringPaint { get; private set; }
    internal int Paints { get; private set; }
    internal int MaximumDepth { get; private set; }
    internal List<string> InputEvents { get; } = new();
    internal List<Rectangle> Clips { get; } = new();

    protected override void OnPaint(PaintEventArgs e)
    {
        ++depth;
        MaximumDepth = Math.Max(MaximumDepth, depth);
        ++Paints;
        Clips.Add(e.ClipRectangle);
        try
        {
            if (InjectInputDuringNextPaint)
            {
                InjectInputDuringNextPaint = false;
                _ = InjectPointer.Invoke(this, new object[] { 6u, 8d, 9d, 0d, 1u });
                _ = InjectKey.Invoke(this, new object[] { 0x04u, true, 0u, false });
                if (DisposeDuringNextPaint)
                {
                    DisposeDuringNextPaint = false;
                    Dispose();
                }
            }
            base.OnPaint(e);
        }
        finally { --depth; }
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        EventObservedDuringPaint |= depth != 0;
        InputEvents.Add("pointer");
        Invalidate(new Rectangle(2, 2, 4, 4));
        base.OnMouseDown(e);
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        EventObservedDuringPaint |= depth != 0;
        InputEvents.Add("key");
        base.OnKeyDown(e);
    }
}

sealed class GeometryPanel : Panel
{
    internal void SetMode(AutoSizeMode mode) => SetAutoSizeMode(mode);
    internal void MakeTransparent()
    {
        SetStyle(ControlStyles.SupportsTransparentBackColor, true);
        BackColor = Color.Transparent;
    }
    internal void MakeOpaque()
    {
        BackColor = Color.White;
        SetStyle(ControlStyles.SupportsTransparentBackColor, false);
    }
}

sealed class PointerPositionProbe : Panel
{
    private static readonly global::System.Reflection.MethodInfo InjectPointer =
        typeof(Control).GetMethod("__InjectManagedPointer",
            global::System.Reflection.BindingFlags.Instance |
            global::System.Reflection.BindingFlags.NonPublic) ??
        throw new InvalidOperationException("managed pointer injection is unavailable");

    internal Point LastClientMousePosition { get; private set; }
    internal void InjectDown(int x, int y) =>
        _ = InjectPointer.Invoke(this, new object[] { 6u, (double)x, (double)y, 0d, 1u });

    protected override void OnMouseDown(MouseEventArgs e)
    {
        LastClientMousePosition = PointToClient(Control.MousePosition);
        base.OnMouseDown(e);
    }
}

sealed class NumericProbe : NumericUpDown
{
    internal void SpinAt(int x, int y) =>
        OnMouseUp(new MouseEventArgs(MouseButtons.Left, 1, x, y, 0));
    internal void WheelBy(int delta) =>
        OnMouseWheel(new MouseEventArgs(MouseButtons.None, 0, Width / 2, Height / 2, delta));
    internal void DragSelect(int startX, int endX)
    {
        OnMouseDown(new MouseEventArgs(MouseButtons.Left, 1, startX, Height / 2, 0));
        OnMouseMove(new MouseEventArgs(MouseButtons.Left, 0, endX, Height / 2, 0));
        OnMouseUp(new MouseEventArgs(MouseButtons.Left, 1, endX, Height / 2, 0));
    }
}

sealed class ScrollProbe : Panel
{
    internal void Wheel(int delta) =>
        OnMouseWheel(new MouseEventArgs(MouseButtons.None, 0, Width / 2, Height / 2, delta));
    internal void RaiseScroll(ScrollEventArgs args) => OnScroll(args);
}

sealed class ScrollablePaintProbe : ScrollableControl
{
    protected override void OnPaint(PaintEventArgs e)
    {
        using var fill = new SolidBrush(Color.Navy);
        e.Graphics.FillRectangle(fill, ClientRectangle);
        base.OnPaint(e);
    }
}

sealed class ThemePaintProbe : Control
{
    internal int PaintCount { get; private set; }
    internal Color LastBackColor { get; private set; }
    internal Color LastForeColor { get; private set; }

    protected override void OnPaint(PaintEventArgs e)
    {
        ++PaintCount;
        LastBackColor = BackColor;
        LastForeColor = ForeColor;
        using var background = new SolidBrush(BackColor);
        using var foreground = new Pen(ForeColor);
        e.Graphics.FillRectangle(background, ClientRectangle);
        e.Graphics.DrawRectangle(foreground, 1, 1,
            Math.Max(0, Width - 3), Math.Max(0, Height - 3));
        base.OnPaint(e);
    }
}

sealed class SplitContainerProbe : SplitContainer
{
    internal void DragSplitter(int start, int end)
    {
        var vertical = Orientation == Orientation.Vertical;
        OnMouseDown(new MouseEventArgs(MouseButtons.Left, 1,
            vertical ? start : Width / 2, vertical ? Height / 2 : start, 0));
        OnMouseMove(new MouseEventArgs(MouseButtons.Left, 0,
            vertical ? end : Width / 2, vertical ? Height / 2 : end, 0));
        OnMouseUp(new MouseEventArgs(MouseButtons.Left, 1,
            vertical ? end : Width / 2, vertical ? Height / 2 : end, 0));
    }
}

sealed class DragSliderProbe : Control
{
    internal int Value { get; private set; }
    internal int MouseDowns { get; private set; }
    internal int MouseUps { get; private set; }
    internal int DragMoves { get; private set; }
    private bool dragging;

    private void SetFromX(int x)
    {
        Value = Math.Clamp((int)Math.Round(Math.Clamp(x, 0, Width) * 100d /
            Math.Max(1, Width)), 0, 100);
        Invalidate();
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button != MouseButtons.Left) return;
        ++MouseDowns;
        dragging = true;
        Capture = true;
        SetFromX(e.X);
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (!dragging || e.Button != MouseButtons.Left) return;
        ++DragMoves;
        SetFromX(e.X);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (!dragging || e.Button != MouseButtons.Left) return;
        ++MouseUps;
        SetFromX(e.X);
        dragging = false;
        Capture = false;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        using var track = new SolidBrush(Color.FromArgb(111, 126, 141));
        using var thumb = new SolidBrush(Color.FromArgb(64, 125, 190));
        e.Graphics.FillRectangle(track, 4, Height / 2 - 2, Math.Max(0, Width - 8), 4);
        var x = 4 + (int)Math.Round(Math.Max(0, Width - 16) * Value / 100d);
        e.Graphics.FillRectangle(thumb, x, 4, 12, Math.Max(1, Height - 8));
    }
}

sealed class StandardClickProbe : Control
{
    internal StandardClickProbe()
    {
        SetStyle(ControlStyles.StandardClick, true);
        TabStop = false;
    }
}

static class NativeSurfaceProbe
{
    [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Sequential)]
    internal struct NativeRect { internal int Left, Top, Right, Bottom; }

    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    [return: global::System.Runtime.InteropServices.MarshalAs(global::System.Runtime.InteropServices.UnmanagedType.Bool)]
    internal static extern bool IsWindow(nint window);
    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    [return: global::System.Runtime.InteropServices.MarshalAs(global::System.Runtime.InteropServices.UnmanagedType.Bool)]
    internal static extern bool IsWindowEnabled(nint window);
    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    [return: global::System.Runtime.InteropServices.MarshalAs(global::System.Runtime.InteropServices.UnmanagedType.Bool)]
    internal static extern bool IsWindowVisible(nint window);
    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    [return: global::System.Runtime.InteropServices.MarshalAs(global::System.Runtime.InteropServices.UnmanagedType.Bool)]
    internal static extern bool GetLayeredWindowAttributes(
        nint window, out uint colorKey, out byte alpha, out uint flags);
    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    [return: global::System.Runtime.InteropServices.MarshalAs(global::System.Runtime.InteropServices.UnmanagedType.Bool)]
    internal static extern bool GetClientRect(nint window, out NativeRect bounds);
    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    internal static extern nint GetDC(nint window);
    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    internal static extern nint GetParent(nint window);
    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    internal static extern int ReleaseDC(nint window, nint device);
    [global::System.Runtime.InteropServices.DllImport("user32.dll")]
    internal static extern int FillRect(nint device, ref NativeRect bounds, nint brush);
    [global::System.Runtime.InteropServices.DllImport("gdi32.dll")]
    internal static extern nint CreateSolidBrush(uint color);
    [global::System.Runtime.InteropServices.DllImport("gdi32.dll")]
    internal static extern uint GetPixel(nint device, int x, int y);
    [global::System.Runtime.InteropServices.DllImport("gdi32.dll")]
    [return: global::System.Runtime.InteropServices.MarshalAs(global::System.Runtime.InteropServices.UnmanagedType.Bool)]
    internal static extern bool DeleteObject(nint value);
}
