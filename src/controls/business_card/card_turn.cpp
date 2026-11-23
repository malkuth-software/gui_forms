#include "card_turn.hpp"
#include "stxlib/math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace gui_forms::detail {
namespace {

constexpr double half_pi = 1.57079632679489661923;

struct Vector3 final {
    double x{};
    double y{};
    double z{};
};

double dot(Vector3 left, Vector3 right) noexcept {
    const double product = left.x * right.x + left.y * right.y + left.z * right.z;
    return product;
}

Vector3 normalized(Vector3 value) noexcept {
    const double length = std::sqrt(dot(value, value));
    if (!(length > 0.0)) return Vector3{0.0, 0.0, 1.0};
    const Vector3 unit{value.x / length, value.y / length, value.z / length};
    return unit;
}

// The card's axes as the pose turns them: across, down and its front's
// normal (Rx(pitch) Ry(yaw) applied to x, y and z).
struct CardAxes final {
    Vector3 across{};
    Vector3 down{};
    Vector3 normal{};
};

CardAxes axes_of(double yaw, double pitch) noexcept {
    // STXlib's deterministic sine and cosine (the same bits on every host).
    double yaw_turn_sine = 0.0;
    double yaw_turn_cosine = 0.0;
    stxlib::math::sincos(yaw, yaw_turn_sine, yaw_turn_cosine);
    double pitch_turn_sine = 0.0;
    double pitch_turn_cosine = 0.0;
    stxlib::math::sincos(pitch, pitch_turn_sine, pitch_turn_cosine);
    const double cos_yaw = yaw_turn_cosine;
    const double sin_yaw = yaw_turn_sine;
    const double cos_pitch = pitch_turn_cosine;
    const double sin_pitch = pitch_turn_sine;
    CardAxes axes{};
    axes.across = Vector3{cos_yaw, sin_yaw * sin_pitch, -sin_yaw * cos_pitch};
    axes.down = Vector3{0.0, cos_pitch, sin_pitch};
    axes.normal = Vector3{sin_yaw, -cos_yaw * sin_pitch, cos_yaw * cos_pitch};
    return axes;
}

// The lamp's direction from the card, above and to the left of the eye.
constexpr Vector3 lamp_direction{-0.35, -0.55, 0.76};

// The ray from the eye through the destination point (x, y) met with the
// card's plane: the face coordinates there, or no hit.
CardTurnHit meet(const CardAxes& axes, const CardTurnPose& pose, double x, double y) noexcept {
    CardTurnHit hit{};
    const double centre_x = pose.card.x + pose.card.width * 0.5;
    const double centre_y = pose.card.y + pose.card.height * 0.5;
    const Vector3 ray{x - centre_x, y - centre_y, -pose.eye};
    const double facing = dot(axes.normal, ray);
    if (std::abs(facing) < 1e-12) return hit;
    // t along the ray from the eye; the eye's offset from the centre is
    // (0, 0, eye).
    const double t = -(axes.normal.z * pose.eye) / facing;
    if (!(t > 0.0)) return hit;
    const Vector3 offset{t * ray.x, t * ray.y, pose.eye * (1.0 - t)};
    const double along = dot(axes.across, offset);
    const double downward = dot(axes.down, offset);
    const double u = along / pose.card.width + 0.5;
    const double v = downward / pose.card.height + 0.5;
    if (!(u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0)) return hit;
    hit.hit = true;
    hit.front = axes.normal.z > 0.0;
    // `u` runs along the card's across axis, from the front's left edge.
    // The back, seen from its own side, starts at the front's right edge,
    // so its own u is 1 - u. At a yaw of pi the across axis already runs
    // right to left on the screen, and the two flips together show the
    // back as painted (card_turn.hpp).
    hit.u = hit.front ? u : 1.0 - u;
    hit.v = v;
    return hit;
}

bool face_fits(const CardFaceView& face) noexcept {
    if (face.width == 0U || face.height == 0U) return false;
    const std::size_t row = static_cast<std::size_t>(face.width) * 4U;
    if (face.row_bytes < row) return false;
    const std::size_t needed = face.row_bytes * (static_cast<std::size_t>(face.height) - 1U) + row;
    const bool fits = face.pixels.size() >= needed;
    return fits;
}

// The face's premultiplied colour at (u, v), bilinearly between its four
// nearest pixel centres, clamped at its edges.
std::array<double, 4> sample_face(const CardFaceView& face, double u, double v) noexcept {
    const double x = std::clamp(u * face.width - 0.5, 0.0, static_cast<double>(face.width - 1U));
    const double y = std::clamp(v * face.height - 0.5, 0.0, static_cast<double>(face.height - 1U));
    const std::uint32_t x0 = static_cast<std::uint32_t>(x);
    const std::uint32_t y0 = static_cast<std::uint32_t>(y);
    const std::uint32_t x1 = std::min(x0 + 1U, face.width - 1U);
    const std::uint32_t y1 = std::min(y0 + 1U, face.height - 1U);
    const double across = x - static_cast<double>(x0);
    const double down = y - static_cast<double>(y0);
    const std::size_t row0 = static_cast<std::size_t>(y0) * face.row_bytes;
    const std::size_t row1 = static_cast<std::size_t>(y1) * face.row_bytes;
    const std::size_t at00 = row0 + static_cast<std::size_t>(x0) * 4U;
    const std::size_t at01 = row0 + static_cast<std::size_t>(x1) * 4U;
    const std::size_t at10 = row1 + static_cast<std::size_t>(x0) * 4U;
    const std::size_t at11 = row1 + static_cast<std::size_t>(x1) * 4U;
    std::array<double, 4> colour{};
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const double top = face.pixels[at00 + channel] * (1.0 - across) + face.pixels[at01 + channel] * across;
        const double bottom = face.pixels[at10 + channel] * (1.0 - across) + face.pixels[at11 + channel] * across;
        colour[channel] = top * (1.0 - down) + bottom * down;
    }
    return colour;
}

std::uint8_t channel_byte(double value) noexcept {
    const double clamped = std::clamp(value, 0.0, 255.0);
    const std::uint8_t byte = static_cast<std::uint8_t>(std::lround(clamped));
    return byte;
}

// The four sub-sample offsets within a device pixel, in pixel units.
constexpr std::array<double, 2> sub_offsets{0.25, 0.75};

} // namespace

bool card_front_faces_eye(double yaw, double pitch) noexcept {
    const double cos_yaw = stxlib::math::cos(yaw);
    const double cos_pitch = stxlib::math::cos(pitch);
    const bool front = cos_yaw * cos_pitch > 0.0;
    return front;
}

bool valid_card_turn_pose(const CardTurnPose& pose) noexcept {
    if (!std::isfinite(pose.yaw) || !std::isfinite(pose.pitch)) return false;
    if (!(std::abs(pose.pitch) < half_pi)) return false;
    if (!pose.card.finite() || !(pose.card.width > 0.0) || !(pose.card.height > 0.0)) return false;
    const double half_diagonal = 0.5 * stxlib::math::hypot(pose.card.width, pose.card.height);
    const bool far = std::isfinite(pose.eye) && pose.eye > half_diagonal;
    return far;
}

Rect turned_card_bounds(const CardTurnPose& pose) noexcept {
    if (!valid_card_turn_pose(pose)) return Rect{};
    const CardAxes axes = axes_of(pose.yaw, pose.pitch);
    const double centre_x = pose.card.x + pose.card.width * 0.5;
    const double centre_y = pose.card.y + pose.card.height * 0.5;
    constexpr std::array<double, 2> signs{-0.5, 0.5};
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    bool first = true;
    for (const double across_sign : signs) {
        for (const double down_sign : signs) {
            const double along = across_sign * pose.card.width;
            const double downward = down_sign * pose.card.height;
            const double corner_x = along * axes.across.x + downward * axes.down.x;
            const double corner_y = along * axes.across.y + downward * axes.down.y;
            const double corner_z = along * axes.across.z + downward * axes.down.z;
            // From the eye through the corner to the rest plane z = 0.
            const double reach = pose.eye / (pose.eye - corner_z);
            const double x = centre_x + corner_x * reach;
            const double y = centre_y + corner_y * reach;
            left = first ? x : std::min(left, x);
            right = first ? x : std::max(right, x);
            top = first ? y : std::min(top, y);
            bottom = first ? y : std::max(bottom, y);
            first = false;
        }
    }
    const Rect covered{left, top, right - left, bottom - top};
    return covered;
}

CardTurnHit turned_card_hit(const CardTurnPose& pose, Point point) noexcept {
    if (!valid_card_turn_pose(pose) || !std::isfinite(point.x) || !std::isfinite(point.y)) return CardTurnHit{};
    const CardAxes axes = axes_of(pose.yaw, pose.pitch);
    const CardTurnHit hit = meet(axes, pose, point.x, point.y);
    return hit;
}

bool draw_turned_card(const CardFaceView& front, const CardFaceView& back, const CardTurnPose& pose,
                      const CardTurnLight& light, double scale, const PartRasterView& destination) noexcept {
    if (validate_raster_view(destination) != RasterViewDefect::none) return false;
    const std::size_t row = static_cast<std::size_t>(destination.extent.width) * 4U;
    for (std::uint32_t y = 0U; y < destination.extent.height; ++y) {
        std::memset(destination.pixels.data() + static_cast<std::size_t>(y) * destination.row_bytes, 0, row);
    }
    if (!valid_card_turn_pose(pose) || !std::isfinite(scale) || !(scale > 0.0)) return false;
    if (!face_fits(front) || !face_fits(back)) return false;
    if (!std::isfinite(light.diffuse) || !std::isfinite(light.sheen) || !std::isfinite(light.shininess)) return false;

    // Chosen once for the frame: the axes, the face shown, and its light.
    const CardAxes axes = axes_of(pose.yaw, pose.pitch);
    const bool front_shown = axes.normal.z > 0.0;
    const CardFaceView& face = front_shown ? front : back;
    const Vector3 facing = front_shown ? axes.normal
                                       : Vector3{-axes.normal.x, -axes.normal.y, -axes.normal.z};
    const Vector3 lamp = normalized(lamp_direction);
    const double diffuse = std::clamp(light.diffuse, 0.0, 1.0);
    const double lit_now = std::max(0.0, dot(facing, lamp));
    // At rest the facing normal is z and the shade is 1.
    const double shade = (1.0 - diffuse) + diffuse * lit_now / lamp.z;
    // The sheen grows as the card leaves rest, so a card at rest shows its
    // face exactly as painted.
    const double turned = std::clamp(4.0 * (1.0 - std::abs(axes.normal.z)), 0.0, 1.0);
    const double sheen = std::clamp(light.sheen, 0.0, 1.0) * turned;
    const double shininess = std::clamp(light.shininess, 1.0, 256.0);
    const double centre_x = pose.card.x + pose.card.width * 0.5;
    const double centre_y = pose.card.y + pose.card.height * 0.5;

    // Only the pixels under the card's projected corners are traced.
    const Rect covered = turned_card_bounds(pose);
    const double first_x = std::floor(covered.x * scale) - 1.0;
    const double first_y = std::floor(covered.y * scale) - 1.0;
    const double last_x = std::ceil(covered.right() * scale) + 1.0;
    const double last_y = std::ceil(covered.bottom() * scale) + 1.0;
    const double width_limit = static_cast<double>(destination.extent.width);
    const double height_limit = static_cast<double>(destination.extent.height);
    const std::uint32_t x_begin = static_cast<std::uint32_t>(std::clamp(first_x, 0.0, width_limit));
    const std::uint32_t y_begin = static_cast<std::uint32_t>(std::clamp(first_y, 0.0, height_limit));
    const std::uint32_t x_end = static_cast<std::uint32_t>(std::clamp(last_x, 0.0, width_limit));
    const std::uint32_t y_end = static_cast<std::uint32_t>(std::clamp(last_y, 0.0, height_limit));

    for (std::uint32_t y = y_begin; y < y_end; ++y) {
        std::uint8_t* const line = destination.pixels.data() + static_cast<std::size_t>(y) * destination.row_bytes;
        for (std::uint32_t x = x_begin; x < x_end; ++x) {
            std::uint32_t inside = 0U;
            CardTurnHit sampled{};
            for (const double sub_y : sub_offsets) {
                for (const double sub_x : sub_offsets) {
                    const double logical_x = (static_cast<double>(x) + sub_x) / scale;
                    const double logical_y = (static_cast<double>(y) + sub_y) / scale;
                    const CardTurnHit hit = meet(axes, pose, logical_x, logical_y);
                    if (!hit.hit) continue;
                    if (inside == 0U) sampled = hit;
                    ++inside;
                }
            }
            if (inside == 0U) continue;
            const std::array<double, 4> colour = sample_face(face, sampled.u, sampled.v);
            const double coverage = static_cast<double>(inside) / 4.0;
            double highlight = 0.0;
            if (sheen > 0.0) {
                const double logical_x = (static_cast<double>(x) + 0.5) / scale;
                const double logical_y = (static_cast<double>(y) + 0.5) / scale;
                // From the card toward the eye, approximated at the pixel's
                // point on the rest plane (the sheen is a soft highlight).
                const Vector3 view = normalized(Vector3{centre_x - logical_x, centre_y - logical_y, pose.eye});
                const Vector3 half = normalized(Vector3{lamp.x + view.x, lamp.y + view.y, lamp.z + view.z});
                highlight = sheen * std::pow(std::max(0.0, dot(facing, half)), shininess);
            }
            const double alpha = colour[3] * coverage;
            std::uint8_t* const pixel = line + static_cast<std::size_t>(x) * 4U;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                const double lit = colour[channel] * shade * coverage + highlight * alpha;
                pixel[channel] = channel_byte(std::min(lit, alpha));
            }
            pixel[3] = channel_byte(alpha);
        }
    }
    return true;
}

} // namespace gui_forms::detail
