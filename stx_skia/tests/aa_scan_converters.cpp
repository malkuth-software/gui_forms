// Differential corpus for the anti-aliased scan converters: analytic path fills (mask,
// run-based and clamped run-based accumulation, convex and general walks, inverse fills,
// fat rectangles), anti-aliased clips built from paths, and anti-aliased hairlines, rectangle
// fills and rectangle frames. A recording blitter captures every call in order, including
// every coverage byte of each blitted mask.
//
// Clip-row invariance (owner decision 2026-10-28): a non-inverse fill walks the path's own
// rows whatever the clip, so a clip whose rows cut the path changes no coverage inside it. The
// cases whose clip rows cut a non-inverse path go to a second corpus, aa-scan-clip-rows.raw,
// which differs from the reference on purpose; the rest must still match. The candidate also
// checks that a fill split into row bands draws exactly the coverage of the whole fill.

#include "differential_corpus.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkSpan.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkMask.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRaw.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkScan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

// Records every blitter call, in order, into a corpus.
class RecordingBlitter : public SkBlitter {
public:
    explicit RecordingBlitter(Output* output) : fOutput(output) {}

    void blitH(int x, int y, int width) override {
        record(1, x, y, width, 0);
    }
    void blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) override {
        record(2, x, y, 0, 0);
        int index = 0;
        while (runs[index] > 0) {
            append(*fOutput, antialias[index]);
            append(*fOutput, runs[index]);
            index += runs[index];
        }
        append(*fOutput, static_cast<int16_t>(0));
    }
    void blitV(int x, int y, int height, SkAlpha alpha) override {
        record(3, x, y, height, alpha);
    }
    void blitRect(int x, int y, int width, int height) override {
        record(4, x, y, width, height);
    }
    void blitAntiRect(int x, int y, int width, int height, SkAlpha left, SkAlpha right) override {
        record(5, x, y, width, height);
        append(*fOutput, left);
        append(*fOutput, right);
    }
    void blitMask(const SkMask& mask, const SkIRect& clip) override {
        record(6, mask.fBounds.fLeft, mask.fBounds.fTop, mask.fBounds.width(),
               mask.fBounds.height());
        append(*fOutput, static_cast<uint8_t>(mask.fFormat));
        append(*fOutput, clip.fLeft);
        append(*fOutput, clip.fTop);
        append(*fOutput, clip.fRight);
        append(*fOutput, clip.fBottom);
        SkIRect visible = mask.fBounds;
        if (mask.fFormat == SkMask::kA8_Format && visible.intersect(clip)) {
            for (int y = visible.fTop; y < visible.fBottom; ++y) {
                for (int x = visible.fLeft; x < visible.fRight; ++x) {
                    append(*fOutput, *mask.getAddr8(x, y));
                }
            }
        }
    }

    int calls() const { return fCalls; }

private:
    void record(uint8_t kind, int a, int b, int c, int d) {
        append(*fOutput, kind);
        append(*fOutput, a);
        append(*fOutput, b);
        append(*fOutput, c);
        append(*fOutput, d);
        ++fCalls;
    }

    Output* fOutput;
    int fCalls = 0;
};

// Writes every blitted coverage byte into an image over coverage_bounds, so that fills under
// different clips can be compared pixel for pixel.
const SkIRect coverage_bounds = SkIRect::MakeLTRB(-64, -64, 512, 512);

class CoverageBlitter : public SkBlitter {
public:
    CoverageBlitter() : fCoverage(static_cast<size_t>(coverage_bounds.width()) *
                                  static_cast<size_t>(coverage_bounds.height())) {}

    void blitH(int x, int y, int width) override {
        for (int i = 0; i < width; ++i) set(x + i, y, 0xFF);
    }
    void blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) override {
        int index = 0;
        while (runs[index] > 0) {
            for (int i = 0; i < runs[index]; ++i) set(x + index + i, y, antialias[index]);
            index += runs[index];
        }
    }
    void blitV(int x, int y, int height, SkAlpha alpha) override {
        for (int i = 0; i < height; ++i) set(x, y + i, alpha);
    }
    void blitRect(int x, int y, int width, int height) override {
        for (int row = 0; row < height; ++row) blitH(x, y + row, width);
    }
    void blitAntiRect(int x, int y, int width, int height, SkAlpha left, SkAlpha right) override {
        blitV(x, y, height, left);
        blitRect(x + 1, y, width, height);
        blitV(x + 1 + width, y, height, right);
    }
    void blitMask(const SkMask& mask, const SkIRect& clip) override {
        if (mask.fFormat != SkMask::kA8_Format) {
            fUnsupported = true;
            return;
        }
        SkIRect visible = mask.fBounds;
        if (!visible.intersect(clip)) return;
        for (int y = visible.fTop; y < visible.fBottom; ++y) {
            for (int x = visible.fLeft; x < visible.fRight; ++x) set(x, y, *mask.getAddr8(x, y));
        }
    }

    // The first pixel whose coverage differs from other's, as (x, y), or nothing.
    std::optional<SkIPoint> firstDifference(const CoverageBlitter& other) const {
        for (size_t index = 0; index < fCoverage.size(); ++index) {
            if (fCoverage[index] != other.fCoverage[index]) {
                const int width = coverage_bounds.width();
                const SkIPoint point = SkIPoint::Make(
                        coverage_bounds.fLeft + static_cast<int>(index % static_cast<size_t>(width)),
                        coverage_bounds.fTop + static_cast<int>(index / static_cast<size_t>(width)));
                return point;
            }
        }
        return std::nullopt;
    }
    uint8_t at(SkIPoint point) const {
        const uint8_t value = fCoverage[offset(point.fX, point.fY)];
        return value;
    }
    bool valid() const { return !fOutside && !fUnsupported; }
    const std::vector<uint8_t>& coverage() const { return fCoverage; }

private:
    size_t offset(int x, int y) const {
        const size_t result = static_cast<size_t>(y - coverage_bounds.fTop) *
                              static_cast<size_t>(coverage_bounds.width()) +
                              static_cast<size_t>(x - coverage_bounds.fLeft);
        return result;
    }
    void set(int x, int y, SkAlpha alpha) {
        if (!coverage_bounds.contains(x, y)) {
            fOutside = true;
            return;
        }
        uint8_t& value = fCoverage[offset(x, y)];
        value = static_cast<uint8_t>(255 - ((255 - value) * (255 - alpha) + 127) / 255);
    }

    std::vector<uint8_t> fCoverage;
    bool fOutside = false;
    bool fUnsupported = false;
};

// Coordinates in [origin, origin + span) at sixteenth steps.
float pick_coordinate(Sequence& sequence, int origin, int span) {
    const uint32_t offset = sequence.next() % static_cast<uint32_t>(span);
    const int32_t whole = origin + static_cast<int32_t>(offset);
    const float fraction = static_cast<float>(sequence.next() % 16) * 0.0625f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkPath random_path(Sequence& sequence) {
    // Small paths use the mask accumulator; larger ones the run-based accumulators.
    const uint32_t size = sequence.next() % 4;
    const int span = size == 0 ? 12 : size == 1 ? 30 : size == 2 ? 90 : 400;
    const int origin = static_cast<int>(sequence.next() % 40) - 20;
    SkPathBuilder builder{};
    const uint32_t shape = sequence.next() % 6;
    if (shape == 0) {
        const float l = pick_coordinate(sequence, origin, span);
        const float t = pick_coordinate(sequence, origin, span);
        builder.addRect(SkRect::MakeLTRB(l, t, l + pick_coordinate(sequence, 0, span),
                                         t + pick_coordinate(sequence, 0, span)));
    } else if (shape == 1) {
        const float cx = pick_coordinate(sequence, origin, span);
        const float cy = pick_coordinate(sequence, origin, span);
        builder.addOval(SkRect::MakeLTRB(cx, cy, cx + pick_coordinate(sequence, 1, span),
                                         cy + pick_coordinate(sequence, 1, span)));
    } else {
        const int contours = 1 + static_cast<int>(sequence.next() % 2);
        for (int contour = 0; contour < contours; ++contour) {
            SkPoint last{pick_coordinate(sequence, origin, span),
                         pick_coordinate(sequence, origin, span)};
            builder.moveTo(last);
            const int verbs = 2 + static_cast<int>(sequence.next() % 5);
            for (int verb = 0; verb < verbs; ++verb) {
                const SkPoint p1{pick_coordinate(sequence, origin, span),
                                 pick_coordinate(sequence, origin, span)};
                const SkPoint p2{pick_coordinate(sequence, origin, span),
                                 pick_coordinate(sequence, origin, span)};
                const SkPoint p3{pick_coordinate(sequence, origin, span),
                                 pick_coordinate(sequence, origin, span)};
                const uint32_t kind = sequence.next() % 4;
                if (kind == 0) {
                    builder.lineTo(p1);
                    last = p1;
                } else if (kind == 1) {
                    builder.quadTo(p1, p2);
                    last = p2;
                } else if (kind == 2) {
                    builder.conicTo(p1, p2, 0.70710678f);
                    last = p2;
                } else {
                    builder.cubicTo(p1, p2, p3);
                    last = p3;
                }
            }
            builder.close();
        }
    }
    const std::array<SkPathFillType, 4> fills{
        SkPathFillType::kWinding, SkPathFillType::kEvenOdd, SkPathFillType::kInverseWinding,
        SkPathFillType::kInverseEvenOdd,
    };
    builder.setFillType(fills[sequence.pick(fills.size())]);
    const SkPath path = builder.detach();
    return path;
}

std::vector<SkRasterClip> make_clips() {
    std::vector<SkRasterClip> clips{};
    clips.reserve(8);
    clips.emplace_back(SkIRect::MakeLTRB(0, 0, 100, 100));
    clips.emplace_back(SkIRect::MakeLTRB(-30, -30, 400, 400));
    clips.emplace_back(SkIRect::MakeLTRB(5, 7, 20, 60));
    const SkPath circle = SkPath::Circle(50, 50, 40);
    clips.emplace_back(circle, SkIRect::MakeLTRB(0, 0, 100, 100), false);
    clips.emplace_back(circle, SkIRect::MakeLTRB(0, 0, 100, 100), true);
    SkPathBuilder holes{};
    holes.addRect(SkRect::MakeLTRB(0, 0, 100, 100));
    holes.addRect(SkRect::MakeLTRB(30, 30, 70, 70), SkPathDirection::kCCW);
    const SkPath ring = holes.detach();
    clips.emplace_back(ring, SkIRect::MakeLTRB(0, 0, 100, 100), true);
    clips.emplace_back(SkIRect::MakeLTRB(0, 0, 40000, 40000));
    return clips;
}

// True when the clip's rows cut a non-inverse path's rows: the fill then walks rows the clip
// leaves out, which the candidate draws differently from the reference on purpose.
bool clip_rows_cut(const SkPath& path, const SkIRect& clipBounds) {
    if (path.isInverseFillType()) return false;
    const SkIRect ir = path.getBounds().roundOut();
    const bool cut = ir.fTop < clipBounds.fTop || ir.fBottom > clipBounds.fBottom;
    return cut;
}

void fill_corpus(Output& stable, Output& clipRows, Sequence& sequence,
                 const std::vector<SkRasterClip>& clips) {
    for (int i = 0; i < 3000; ++i) {
        const SkPath path = random_path(sequence);
        const SkRasterClip& clip = clips[sequence.pick(clips.size())];
        Output& output = clip_rows_cut(path, clip.getBounds()) ? clipRows : stable;
        for (int resolve = 0; resolve < 2; ++resolve) {
            const std::optional<SkPathRaw> raw = SkPathPriv::Raw(
                    path, resolve == 0 ? SkResolveConvexity::kNo : SkResolveConvexity::kYes);
            append_bool(output, raw.has_value());
            if (!raw.has_value()) {
                continue;
            }
            RecordingBlitter blitter(&output);
            SkScan::AntiFillPath(raw.value(), clip, &blitter);
            append(output, blitter.calls());
        }
        ++output.cases;
    }
}

void aa_clip_corpus(Output& stable, Output& clipRows, Sequence& sequence) {
    for (int i = 0; i < 600; ++i) {
        const SkPath path = random_path(sequence);
        const SkIRect bounds = SkIRect::MakeLTRB(-10, -10, 120 + static_cast<int>(i % 300),
                                                 110);
        Output& output = clip_rows_cut(path, bounds) ? clipRows : stable;
        // Building an anti-aliased clip fills the path through the run-based accumulator.
        const SkRasterClip clip(path, bounds, true);
        append_bool(output, clip.isEmpty());
        append_bool(output, clip.isBW());
        append_bool(output, clip.isRect());
        const SkIRect& clipBounds = clip.getBounds();
        append(output, clipBounds.fLeft);
        append(output, clipBounds.fTop);
        append(output, clipBounds.fRight);
        append(output, clipBounds.fBottom);
        RecordingBlitter blitter(&output);
        SkScan::FillRect(SkRect::MakeLTRB(-20, -20, 500, 200), clip, &blitter);
        append(output, blitter.calls());
        ++output.cases;
    }
}

void rect_and_hairline_corpus(Output& output, Sequence& sequence,
                              const std::vector<SkRasterClip>& clips) {
    for (int i = 0; i < 2500; ++i) {
        const SkRasterClip& clip = clips[sequence.pick(clips.size())];
        const SkRect rect = SkRect::MakeLTRB(pick_coordinate(sequence, -20, 140),
                                             pick_coordinate(sequence, -20, 140),
                                             pick_coordinate(sequence, -20, 140),
                                             pick_coordinate(sequence, -20, 140));
        RecordingBlitter blitter(&output);
        SkScan::AntiFillRect(rect, clip, &blitter);
        SkScan::AntiHairRect(rect, clip, &blitter);
        const std::array<float, 6> strokes{0.0f, 0.25f, 0.5f, 1.0f, 2.5f, 7.0f};
        const SkPoint stroke{strokes[sequence.pick(strokes.size())],
                             strokes[sequence.pick(strokes.size())]};
        SkScan::AntiFrameRect(rect, stroke, clip, &blitter);
        SkXRect xrect = SkIRect::MakeEmpty();
        XRect_set(&xrect, rect.makeSorted());
        SkScan::AntiFillXRect(xrect, clip, &blitter);

        std::array<SkPoint, 5> pts{};
        for (SkPoint& point : pts) {
            point = SkPoint{pick_coordinate(sequence, -40, 180),
                            pick_coordinate(sequence, -40, 180)};
        }
        if (i % 7 == 0) {
            pts[1] = SkPoint{pts[0].fX, pts[1].fY};  // vertical
        } else if (i % 7 == 1) {
            pts[1] = SkPoint{pts[1].fX, pts[0].fY};  // horizontal
        } else if (i % 7 == 2) {
            pts[1] = SkPoint{pts[0].fX + 2000, pts[0].fY + 3};  // long, subdivided
        }
        const size_t count = 2 + sequence.pick(pts.size() - 1);
        SkScan::AntiHairLine(SkSpan<const SkPoint>(pts.data(), count), clip, &blitter);
        append(output, blitter.calls());
        ++output.cases;
    }
}

// Fills path under clip into coverage; false when the raw path is not available.
bool fill_coverage(const SkPath& path, const SkIRect& clip, CoverageBlitter& coverage) {
    const std::optional<SkPathRaw> raw = SkPathPriv::Raw(path, SkResolveConvexity::kYes);
    if (!raw.has_value()) return false;
    const SkRasterClip rasterClip(clip);
    SkScan::AntiFillPath(raw.value(), rasterClip, &coverage);
    return true;
}

// One clip-row case: the whole fill against the fill split into row bands at the given cut
// rows. Appends the banded coverage to the corpus; returns false when the candidate's bands
// differ from the whole fill.
bool band_case(Output& clipRows, const char* name, const SkPath& path,
               const std::vector<int>& cuts) {
    CoverageBlitter whole{};
    if (!fill_coverage(path, coverage_bounds, whole)) return true;
    CoverageBlitter banded{};
    int top = coverage_bounds.fTop;
    for (size_t index = 0; index <= cuts.size(); ++index) {
        const int bottom = index < cuts.size() ? cuts[index] : coverage_bounds.fBottom;
        SkIRect band = SkIRect::MakeLTRB(coverage_bounds.fLeft, top, coverage_bounds.fRight, bottom);
        if (band.intersect(coverage_bounds)) static_cast<void>(fill_coverage(path, band, banded));
        top = bottom;
    }
    corpus::append(clipRows, corpus::fnv1a(banded.coverage()));
    ++clipRows.cases;
#ifndef STX_SKIA_REFERENCE
    const std::optional<SkIPoint> difference = whole.firstDifference(banded);
    if (!whole.valid() || !banded.valid() || difference.has_value()) {
        if (difference.has_value()) {
            std::printf("aa scan converters: %s: banded coverage %d differs from %d at (%d, %d)\n",
                        name, banded.at(difference.value()), whole.at(difference.value()),
                        difference.value().fX, difference.value().fY);
        } else {
            std::printf("aa scan converters: %s: coverage left its image\n", name);
        }
        return false;
    }
#else
    static_cast<void>(name);
#endif
    return true;
}

// The seam GUI.Forms found (2026-10-28): a rounded-rectangle stroke, as a ring of two rounded
// rectangles, and a rounded fill, each cut by a damage edge or band edge one row inside them.
SkPath rounded_ring(const SkRect& outer, float radius, float width) {
    SkPathBuilder builder{};
    builder.addRRect(SkRRect::MakeRectXY(outer, radius, radius));
    builder.addRRect(SkRRect::MakeRectXY(outer.makeInset(width, width), radius - width,
                                         radius - width),
                     SkPathDirection::kCCW);
    const SkPath path = builder.detach();
    return path;
}

bool clip_row_corpus(Output& clipRows, Sequence& sequence) {
    bool same = true;
    const SkPath ring = rounded_ring(SkRect::MakeXYWH(0.5f, 29.5f, 389.0f, 183.107f), 5.5f, 1.0f);
    same = band_case(clipRows, "rounded stroke cut near its bottom", ring, {211}) && same;
    same = band_case(clipRows, "rounded stroke cut through its corner", ring, {32, 209}) && same;
    SkPathBuilder fill{};
    fill.addRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(18.0f, 9.5f, 11.0f, 11.0f), 5.5f, 5.5f));
    same = band_case(clipRows, "small rounded fill cut", fill.detach(), {12, 15}) && same;
    for (int i = 0; i < 1500; ++i) {
        SkPath path = random_path(sequence);
        if (path.isInverseFillType()) path.toggleInverseFillType();
        const SkIRect ir = path.getBounds().roundOut();
        std::vector<int> cuts{};
        if (ir.height() > 1) {
            const int count = 1 + static_cast<int>(sequence.next() % 3);
            int last = ir.fTop;
            for (int cut = 0; cut < count; ++cut) {
                const int room = ir.fBottom - last;
                if (room <= 1) break;
                last += 1 + static_cast<int>(sequence.next() % static_cast<uint32_t>(room - 1));
                cuts.push_back(last);
            }
        }
        same = band_case(clipRows, "random path", path, cuts) && same;
    }
    return same;
}

// The same seam through a canvas, in pixels: a fill drawn in two row bands, the clip cutting it
// at each row in turn, must leave every pixel as the whole fill left it. This also covers the
// real blitters, which blend blitAntiH2 and blitAntiH with different rounding.
struct CanvasShape {
    const char* name;
    SkRRect outer;
    SkRRect inner;
    SkColor color;
};

constexpr int canvas_width = 256;
constexpr int canvas_height = 128;

void paint_shape(SkCanvas& canvas, const CanvasShape& shape) {
    SkPaint background{};
    background.setColor(SkColorSetARGB(255, 240, 240, 236));
    canvas.drawRect(SkRect::MakeWH(canvas_width, canvas_height), background);
    SkPaint paint{};
    paint.setAntiAlias(true);
    paint.setColor(shape.color);
    if (shape.inner.isEmpty()) {
        canvas.drawRRect(shape.outer, paint);
    } else {
        canvas.drawDRRect(shape.outer, shape.inner, paint);
    }
}

// Returns false, after printing the first differing pixel, when a banded paint differs.
bool canvas_band_case(Output& clipRows, const CanvasShape& shape) {
    SkBitmap whole{};
    whole.allocN32Pixels(canvas_width, canvas_height);
    SkCanvas wholeCanvas(whole);
    paint_shape(wholeCanvas, shape);
    const size_t bytes = whole.computeByteSize();
    bool same = true;
    for (int cut = 0; cut <= canvas_height; ++cut) {
        SkBitmap part{};
        part.allocN32Pixels(canvas_width, canvas_height);
        SkCanvas canvas(part);
        for (int band = 0; band < 2; ++band) {
            canvas.save();
            canvas.clipRect(band == 0 ? SkRect::MakeLTRB(0, 0, canvas_width, cut)
                                      : SkRect::MakeLTRB(0, cut, canvas_width, canvas_height));
            paint_shape(canvas, shape);
            canvas.restore();
        }
        const uint8_t* const pixels = static_cast<const uint8_t*>(part.getPixels());
        corpus::append(clipRows, corpus::fnv1a(std::vector<uint8_t>(pixels, pixels + bytes)));
        ++clipRows.cases;
#ifndef STX_SKIA_REFERENCE
        if (same && std::memcmp(pixels, whole.getPixels(), bytes) != 0) {
            for (size_t index = 0; index < bytes; ++index) {
                if (pixels[index] != static_cast<const uint8_t*>(whole.getPixels())[index]) {
                    const size_t pixel = index / 4;
                    std::printf("aa scan converters: canvas %s cut at row %d: pixel (%zu, %zu) "
                                "differs\n",
                                shape.name, cut, pixel % canvas_width, pixel / canvas_width);
                    break;
                }
            }
            same = false;
        }
#endif
    }
    return same;
}

bool canvas_band_corpus(Output& clipRows) {
    // Device geometry as GUI.Forms snaps it: a 1-pixel rounded stroke as two rounded
    // rectangles, a translucent rounded fill at scale 1 and a small one at scale 3.
    const CanvasShape shapes[] = {
            {"rounded stroke",
             SkRRect::MakeRectXY(SkRect::MakeLTRB(0, 9, 190, 93), 6, 6),
             SkRRect::MakeRectXY(SkRect::MakeLTRB(1, 10, 189, 92), 5, 5),
             SkColorSetARGB(255, 30, 60, 200)},
            {"translucent rounded fill",
             SkRRect::MakeRectXY(SkRect::MakeLTRB(120, 41, 181, 88), 9, 9),
             SkRRect{},
             SkColorSetARGB(180, 30, 60, 200)},
            {"small rounded fill",
             SkRRect::MakeRectXY(SkRect::MakeLTRB(54, 28, 87, 61), 16.5f, 16.5f),
             SkRRect{},
             SkColorSetARGB(255, 30, 60, 200)},
            {"fractional rounded stroke",
             SkRRect::MakeRectXY(SkRect::MakeLTRB(20.5f, 30.5f, 95.5f, 62.063f), 2.5f, 2.5f),
             SkRRect::MakeRectXY(SkRect::MakeLTRB(21.5f, 31.5f, 94.5f, 61.063f), 1.5f, 1.5f),
             SkColorSetARGB(200, 120, 20, 40)},
    };
    bool same = true;
    for (const CanvasShape& shape : shapes) {
        same = canvas_band_case(clipRows, shape) && same;
    }
    return same;
}

bool aa_scan_corpus(Output& output, Output& clipRows) {
    Sequence sequence{};
    const std::vector<SkRasterClip> clips = make_clips();
    fill_corpus(output, clipRows, sequence, clips);
    aa_clip_corpus(output, clipRows, sequence);
    rect_and_hairline_corpus(output, sequence, clips);
    const bool rowsSame = clip_row_corpus(clipRows, sequence);
    const bool canvasSame = canvas_band_corpus(clipRows);
    return rowsSame && canvasSame;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 3) return 2;
    Output output{};
    Output clipRows{};
    const bool same = aa_scan_corpus(output, clipRows);
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], clipRows)) return 2;
    if (!same) {
        std::puts("aa scan converters: banded fills differ from whole fills FAIL");
        return 1;
    }
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long clipRowsHash = corpus::fnv1a(clipRows.bytes);
    std::printf("aa scan converters: %zu cases, %zu bytes, hash=%llu; clip rows %zu cases, "
                "hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, clipRows.cases, clipRowsHash);
    return 0;
}
