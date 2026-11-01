#include "cut_off_text_reveal.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <utility>

namespace gui_forms::detail {

namespace {

// Room after the text, so that its last glyph does not touch the face's
// edge; and the least distance the face keeps from the client's edges.
constexpr double end_room = 6.0;
constexpr double client_inset = 2.0;
// A text no wider than its cell by this much is not cut off (measurement
// rounding).
constexpr double fit_tolerance = 0.5;

// The full-window layer a reveal sits in: drawn on the overlay plane,
// never hit by the pointer, never focused.
class CutOffTextLayer final : public Control {
public:
    explicit CutOffTextLayer(StableId stable_id) : Control(std::move(stable_id)) {
        set_paint_plane(PaintPlane::overlay);
        set_focusable(false);
    }
    [[nodiscard]] bool hit_test_local(Point) const override { return false; }
};

} // namespace

// The revealed row: the face over the whole reveal and the text at the
// cell's position within it. `text_bounds_` is in the face's coordinates.
class CutOffTextFace final : public Control {
public:
    CutOffTextFace(StableId stable_id, std::string text, FontSpec font, Rect text_bounds, double baseline,
                   RevealPaint paint)
        : Control(std::move(stable_id)), text_(std::move(text)), font_(font), text_bounds_(text_bounds),
          baseline_(baseline), paint_(std::move(paint)) {
        set_paint_plane(PaintPlane::overlay);
        set_focusable(false);
    }

    void on_paint(Painter& painter, Rect) override {
        const Rect arranged = arranged_bounds();
        const Rect bounds{0.0, 0.0, arranged.width, arranged.height};
        if (const ThemedRevealPaint* themed = std::get_if<ThemedRevealPaint>(&paint_)) {
            PartPaint face;
            face.role = PartRole::face;
            face.family = (*themed).family;
            face.states = (*themed).states;
            face.bounds = bounds;
            paint_part(painter, *this, face);
            PartPaint content = face;
            content.role = PartRole::content;
            content.bounds = text_bounds_;
            content.text = text_;
            content.font = font_;
            content.alignment = PartTextAlignment::start;
            paint_part(painter, *this, content);
            return;
        }
        const PlainRevealPaint& plain = std::get<PlainRevealPaint>(paint_);
        painter.fill_rect(bounds, plain.face);
        const Rect edge{0.5, 0.5, std::max(0.0, bounds.width - 1.0), std::max(0.0, bounds.height - 1.0)};
        painter.stroke_rect(edge, plain.border, 1.0);
        painter.draw_text_utf8({text_bounds_.x, baseline_}, text_, font_, plain.text);
    }

    [[nodiscard]] bool hit_test_local(Point) const override { return false; }

    // The row's own node already carries the whole text; the reveal adds
    // nothing for accessibility.
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.exposed = false;
        return descriptor;
    }

private:
    std::string text_;
    FontSpec font_;
    Rect text_bounds_;
    double baseline_;
    RevealPaint paint_;
};

CutOffTextRevealer::~CutOffTextRevealer() {
    conceal();
}

bool CutOffTextRevealer::reveal(const std::shared_ptr<Control>& owner, CutOffText text) {
    if (revealing() && key_ == text.key) return true;
    conceal();
    if (!owner || text.text.empty()) return false;
    Window* host = (*owner).attached_window();
    if (host == nullptr) return false;
    const ResolvedTextLayout measured = (*owner).resolve_text_layout_utf8(text.text, text.font);
    const double text_width = measured.logical_size.width;
    if (text_width <= text.cell.width + fit_tolerance) return false;

    // Over the row from its left edge, as wide as the row or the whole text
    // needs; moved left, then cut, to stay inside the client.
    const Size client = (*host).client_size();
    const double needed = text.cell.x - text.row.x + text_width + end_room;
    double width = std::max(text.row.width, needed);
    width = std::min(width, std::max(0.0, client.width - 2.0 * client_inset));
    double x = text.row.x;
    if (x + width > client.width - client_inset) x = std::max(client_inset, client.width - client_inset - width);
    const Rect face_bounds{x, text.row.y, width, text.row.height};
    const Rect text_bounds{text.cell.x - x, text.cell.y - text.row.y, text_width + end_room, text.cell.height};

    const std::string prefix = std::string((*owner).stable_id().value()) + ".reveal";
    std::shared_ptr<Control> layer = make_control<CutOffTextLayer>(StableId(prefix + ".layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    std::shared_ptr<CutOffTextFace> face = make_control<CutOffTextFace>(
        StableId(prefix + ".face"), std::move(text.text), text.font, text_bounds, text.baseline, std::move(text.paint));
    (*face).set_requested_bounds(face_bounds);
    (*layer).add_child(face);
    PopupToken token = (*host).open_popup(owner, layer);
    if (!token.connected()) return false;
    layer_ = std::move(layer);
    face_ = std::move(face);
    token_ = std::move(token);
    key_ = std::move(text.key);
    bounds_ = face_bounds;
    return true;
}

void CutOffTextRevealer::conceal() noexcept {
    token_.disconnect();
    face_.reset();
    layer_.reset();
    key_.clear();
    bounds_ = {};
}

bool CutOffTextRevealer::revealing() const noexcept {
    return token_.connected();
}

std::string_view CutOffTextRevealer::key() const noexcept {
    if (!revealing()) return {};
    return key_;
}

Rect CutOffTextRevealer::bounds() const noexcept {
    if (!revealing()) return {};
    return bounds_;
}

} // namespace gui_forms::detail
