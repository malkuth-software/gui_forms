#include "gui_forms/window/backdrop/backdrop_chain.hpp"

#include <cmath>
#include <cstddef>

namespace gui_forms {

namespace {

bool within(double value, double maximum) noexcept {
    const bool inside = std::isfinite(value) && value >= 0.0 && value <= maximum;
    return inside;
}

} // namespace

const char* backdrop_operation_name(BackdropOperationKind kind) noexcept {
    switch (kind) {
    case BackdropOperationKind::blur: return "blur";
    case BackdropOperationKind::saturate: return "saturate";
    case BackdropOperationKind::tint: return "tint";
    }
    return "unknown";
}

BackdropOperation BackdropOperation::blur(double radius) noexcept {
    BackdropOperation operation{};
    operation.kind = BackdropOperationKind::blur;
    operation.amount = radius;
    return operation;
}

BackdropOperation BackdropOperation::saturate(double factor) noexcept {
    BackdropOperation operation{};
    operation.kind = BackdropOperationKind::saturate;
    operation.amount = factor;
    return operation;
}

BackdropOperation BackdropOperation::tint(Color colour) noexcept {
    BackdropOperation operation{};
    operation.kind = BackdropOperationKind::tint;
    operation.colour = colour;
    return operation;
}

bool BackdropChain::append(const BackdropOperation& operation) noexcept {
    if (length >= maximum_operations) return false;
    operations[length] = operation;
    length = length + 1U;
    return true;
}

bool backdrop_chain_valid(const BackdropChain& chain) noexcept {
    if (chain.length > BackdropChain::maximum_operations) return false;
    std::size_t blurs = 0U;
    for (std::size_t index = 0U; index < chain.length; ++index) {
        const BackdropOperation& operation = chain.operations[index];
        switch (operation.kind) {
        case BackdropOperationKind::blur:
            if (!within(operation.amount, backdrop_maximum_blur)) return false;
            blurs = blurs + 1U;
            break;
        case BackdropOperationKind::saturate:
            if (!within(operation.amount, backdrop_maximum_saturation)) return false;
            break;
        case BackdropOperationKind::tint:
            if (operation.amount != 0.0) return false;
            break;
        default: return false;
        }
    }
    const bool valid = blurs <= 1U;
    return valid;
}

Color backdrop_chain_solid(const BackdropChain& chain) noexcept {
    // Premultiplied accumulation over clear, in units of 1.
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 0.0;
    const std::size_t length = chain.length < BackdropChain::maximum_operations ? chain.length
                                                                                : BackdropChain::maximum_operations;
    for (std::size_t index = 0U; index < length; ++index) {
        const BackdropOperation& operation = chain.operations[index];
        if (operation.kind != BackdropOperationKind::tint) continue;
        const double over_alpha = static_cast<double>(operation.colour.alpha) / 255.0;
        const double keep = 1.0 - over_alpha;
        red = static_cast<double>(operation.colour.red) / 255.0 * over_alpha + red * keep;
        green = static_cast<double>(operation.colour.green) / 255.0 * over_alpha + green * keep;
        blue = static_cast<double>(operation.colour.blue) / 255.0 * over_alpha + blue * keep;
        alpha = over_alpha + alpha * keep;
    }
    if (!(alpha > 0.0)) return Color::rgba(0, 0, 0, 0);
    // Back to straight alpha, rounded to the nearest step.
    const Color solid = Color::rgba(static_cast<std::uint8_t>(std::lround(red / alpha * 255.0)),
                                    static_cast<std::uint8_t>(std::lround(green / alpha * 255.0)),
                                    static_cast<std::uint8_t>(std::lround(blue / alpha * 255.0)),
                                    static_cast<std::uint8_t>(std::lround(alpha * 255.0)));
    return solid;
}

BackdropChainResolution resolve_backdrop_chain(const BackdropChain& chain, const WindowBackdropGrant& grant,
                                               bool high_contrast) noexcept {
    BackdropChainResolution resolution{};
    if (high_contrast) {
        resolution.withdrawn = true;
        return resolution;
    }
    const bool material =
        grant.granted == WindowBackdrop::window_material || grant.granted == WindowBackdrop::transient_material;
    const bool blurred = backdrop_chain_blur(chain) > 0.0;
    resolution.blur_by_material = blurred && material;
    resolution.blur_dropped = blurred && !material;
    const std::size_t length = chain.length < BackdropChain::maximum_operations ? chain.length
                                                                                : BackdropChain::maximum_operations;
    for (std::size_t index = 0U; index < length; ++index) {
        if (chain.operations[index].kind == BackdropOperationKind::saturate) resolution.saturate_dropped = true;
    }
    resolution.tint = backdrop_chain_solid(chain);
    resolution.solid = grant.granted == WindowBackdrop::opaque;
    return resolution;
}

double backdrop_chain_blur(const BackdropChain& chain) noexcept {
    double radius = 0.0;
    const std::size_t length = chain.length < BackdropChain::maximum_operations ? chain.length
                                                                                : BackdropChain::maximum_operations;
    for (std::size_t index = 0U; index < length; ++index) {
        const BackdropOperation& operation = chain.operations[index];
        if (operation.kind == BackdropOperationKind::blur && operation.amount > radius) radius = operation.amount;
    }
    return radius;
}

} // namespace gui_forms
