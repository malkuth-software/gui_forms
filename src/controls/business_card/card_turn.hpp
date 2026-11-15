#pragma once

// The business card turned in three dimensions (business_card.hpp, the
// card in the hand): its two faces, painted flat through the theme engine
// into device pixels, are drawn as the card now stands, one ray per
// destination sample. The eye looks down the screen's z axis at the card's
// centre from `eye` logical units away; space is the screen's, x across,
// y down and z toward the eye. The card turns by `yaw` about its vertical
// axis and then by `pitch` about the screen's horizontal one, so that a
// yaw of pi shows the back as it was painted, reading left to right, as a
// card turned over side to side does: the card's own across axis then runs
// right to left on the screen, and the back's own left lies at the front's
// right edge, so the two flips cancel. The pitch stays inside (-pi/2, pi/2):
// only the yaw turns the card over.
//
// Each destination pixel spends four rays (a 2 by 2 grid) to find how much
// of it the card covers, and samples the face it meets once, bilinearly, at
// the first ray that meets it. The face's colour is then lit as the card
// now faces the lamp (CardTurnLight). Every host writes the same bytes:
// the kernel is portable C++ in double precision.
//
// Internal to the business card; no Skia or host type appears here.

#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::detail {

// One face of the card as painted flat, seen from its own side:
// premultiplied BGRA8, `width` by `height` device pixels, row y starting
// at byte y * row_bytes. Borrowed for the call that reads it.
struct CardFaceView final {
    std::span<const std::uint8_t> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_bytes{};
};

// How the card stands. `card` is its rectangle at rest, in the
// destination's logical units (its centre is the turn's centre); `eye` is
// the eye's distance from the card's plane at rest, in logical units, and
// must exceed half the card's diagonal, so the card never reaches the eye.
struct CardTurnPose final {
    double yaw{};
    double pitch{};
    Rect card{};
    double eye{};
};

// How the stock is lit as it turns. The lamp lies above and to the left of
// the eye. At rest the factors cancel, so a card facing the eye shows its
// face exactly as painted; turned, its face darkens as it leaves the lamp,
// by up to `diffuse` of its brightness, and the varnish's `sheen` (0 to 1)
// slides across it as a white highlight of `shininess` (its exponent).
struct CardTurnLight final {
    double diffuse{0.35};
    double sheen{0.12};
    double shininess{28.0};
};

// Where a point of the destination (logical units) meets the card: which
// face, and where on it, `u` across and `v` down that face as seen from its
// own side (0 to 1 each).
struct CardTurnHit final {
    bool hit{};
    bool front{};
    double u{};
    double v{};
};

// Whether the card so turned shows the eye its front.
[[nodiscard]] bool card_front_faces_eye(double yaw, double pitch) noexcept;
// The pose's checks: a finite yaw, a pitch inside (-pi/2, pi/2), a
// non-empty finite card and an eye farther than half its diagonal.
[[nodiscard]] bool valid_card_turn_pose(const CardTurnPose& pose) noexcept;
// The logical rectangle the turned card covers (its four corners
// projected), or an empty one for an invalid pose.
[[nodiscard]] Rect turned_card_bounds(const CardTurnPose& pose) noexcept;
// The face under `point`, or no hit (also for an invalid pose).
[[nodiscard]] CardTurnHit turned_card_hit(const CardTurnPose& pose, Point point) noexcept;

// Fills `destination`, which covers the logical rectangle from (0, 0) at
// `scale` device pixels a logical unit: every pixel transparent, then the
// card as `pose` holds it. Returns false, leaving the destination
// transparent, for an invalid pose, scale or view, or a face whose pixels
// do not fill its shape; nothing else is written. `front` and `back` must
// not overlap the destination.
bool draw_turned_card(const CardFaceView& front, const CardFaceView& back, const CardTurnPose& pose,
                      const CardTurnLight& light, double scale, const PartRasterView& destination) noexcept;

} // namespace gui_forms::detail
