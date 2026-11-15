using System;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

using var bitmap = new Bitmap(32, 24, PixelFormat.Format32bppPArgb);
using (var graphics = Graphics.FromImage(bitmap))
{
    graphics.Clear(Color.White);
    using var blue = new SolidBrush(Color.Blue);
    using var red = new Pen(Color.Red, 2f) { DashStyle = DashStyle.Dash };
    using var font = new Font("Lucida Grande", 10f, FontStyle.Regular,
                              GraphicsUnit.Pixel, 1);
    graphics.FillRectangle(blue, 2, 2, 10, 8);
    graphics.DrawRectangle(red, 1, 1, 14, 12);
    graphics.DrawLine(red, 0, 23, 31, 0);
    graphics.DrawString("M11f", font, blue, 16f, 8f);
}

Require(bitmap.GetPixel(3, 3).B > 240, "native fill pixel");
var textPixels = 0;
for (var y = 7; y < bitmap.Height; ++y)
    for (var x = 16; x < bitmap.Width; ++x)
    {
        var pixel = bitmap.GetPixel(x, y);
        if (pixel.B > 160 && pixel.B > pixel.R + 40) ++textPixels;
    }
Require(textPixels >= 3, "native packaged-font glyph raster");
using var stream = new MemoryStream();
bitmap.Save(stream, ImageFormat.Png);
Require(stream.Length > 64, "native PNG encode");
stream.Position = 0;
using var decoded = (Bitmap)Image.FromStream(stream);
Require(decoded.Width == 32 && decoded.Height == 24, "native PNG dimensions");
Require(decoded.GetPixel(3, 3).B > 240, "native PNG round trip");
using var converted = (Bitmap?)TypeDescriptor.GetConverter(typeof(Bitmap))
    .ConvertFrom(stream.ToArray());
Require(converted is not null && converted.Width == 32 && converted.Height == 24,
        "resource TypeConverter byte-array PNG");

var data = decoded.LockBits(new Rectangle(0, 0, decoded.Width, decoded.Height),
                            ImageLockMode.ReadOnly,
                            PixelFormat.Format32bppPArgb);
Require(data.Scan0 != 0 && data.Stride == decoded.Width * 4,
        "native bitmap lease");
decoded.UnlockBits(data);

using var source = new Bitmap(2, 2);
using (var sourceGraphics = Graphics.FromImage(source)) sourceGraphics.Clear(Color.Blue);
using var transformed = new Bitmap(8, 8);
using var attributes = new ImageAttributes();
var matrix = new ColorMatrix { Matrix33 = 0.5f };
attributes.SetColorMatrix(matrix, ColorMatrixFlag.Default, ColorAdjustType.Bitmap);
attributes.SetRemapTable([new ColorMap { OldColor = Color.Blue, NewColor = Color.Yellow }]);
using (var transformedGraphics = Graphics.FromImage(transformed))
{
    transformedGraphics.DrawImage(source, new Rectangle(0, 0, 8, 8),
                                  0, 0, 2, 2, GraphicsUnit.Pixel, attributes);
}
var adjusted = transformed.GetPixel(1, 1);
Require(adjusted.R > 240 && adjusted.G > 240 && adjusted.B < 8 && adjusted.A == 128,
        "native remap and color matrix");

using var path = new GraphicsPath();
path.AddRectangle(new RectangleF(0, 0, 12, 12));
path.AddEllipse(new RectangleF(2, 2, 8, 8));
path.AddArc(new RectangleF(1, 1, 10, 10), 0, 90);
using var pathClone = (GraphicsPath)path.Clone();
using var transform = new Matrix();
transform.Translate(2, 1);
pathClone.Transform(transform);
Require(path.PathPoints.Length >= 10 && path.GetBounds().Width == 12,
        "native path vocabulary");
// Flatten (graphics_path_flatten, ABI 1.1 provisional): the ellipse
// becomes a closed polygon whose points all lie on or inside its box, and
// a tighter flatness needs more of them.
using var flattened = new GraphicsPath();
flattened.AddEllipse(new RectangleF(0, 0, 40, 20));
using var flattenedFine = (GraphicsPath)flattened.Clone();
flattened.Flatten();
flattenedFine.Flatten(null, 0.01f);
var coarsePoints = flattened.PathPoints;
Require(coarsePoints.Length >= 8 && flattenedFine.PathPoints.Length > coarsePoints.Length,
        "path flattening at a stated flatness");
foreach (var point in coarsePoints)
    Require(point.X >= -0.001f && point.X <= 40.001f && point.Y >= -0.001f && point.Y <= 20.001f,
            "flattened points stay on the ellipse's box");
using var gradientBitmap = new Bitmap(16, 16);
using (var gradientGraphics = Graphics.FromImage(gradientBitmap))
using (var gradient = new LinearGradientBrush(new Rectangle(0, 0, 16, 16),
                                               Color.Red, Color.Blue,
                                               LinearGradientMode.Horizontal))
{
    gradient.InterpolationColors = new ColorBlend(3)
    {
        Colors = [Color.Red, Color.White, Color.Blue],
        Positions = [0f, 0.5f, 1f],
    };
    gradientGraphics.FillPath(gradient, path);
}
Require(gradientBitmap.GetPixel(1, 6).R > gradientBitmap.GetPixel(1, 6).B,
        "native gradient path fill");

using var region = new Region(new Rectangle(0, 0, 16, 16));
region.Exclude(new Rectangle(4, 4, 4, 4));
region.Union(path);

Console.WriteLine($"drawing-facade=pass|png:{stream.Length}|size:{decoded.Width}x{decoded.Height}|text-pixels:{textPixels}");
return 0;

static void Require(bool condition, string name)
{
    if (!condition) throw new InvalidOperationException("drawing facade smoke failed: " + name);
}
