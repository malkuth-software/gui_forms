#pragma once

// A comment may name what the header does not use: no AppKit, no Skia's
// SkCanvas, no CoreGraphics or CGContext, no CoreText, NSView or Metal.

/* A block comment may too: SkPaint, AppKit,
   CoreText * and CGContext ** across lines. */
inline int comments_only_value() { return 1; } /* SkSurface */ // NSView
