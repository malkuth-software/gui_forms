#include "offscreen_raster_parts.hpp"

#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>

namespace gui_forms {

// -- the create result -----------------------------------------------------------

OffscreenRasterCreateResult::OffscreenRasterCreateResult(
    std::variant<std::unique_ptr<OffscreenRaster>, Refusal> state)
    : state_(std::move(state)) {}

OffscreenRasterCreateResult::OffscreenRasterCreateResult(OffscreenRasterCreateResult&&) noexcept = default;
OffscreenRasterCreateResult& OffscreenRasterCreateResult::operator=(OffscreenRasterCreateResult&&) noexcept = default;
OffscreenRasterCreateResult::~OffscreenRasterCreateResult() = default;

OffscreenRasterCreateResult OffscreenRasterCreateResult::created(std::unique_ptr<OffscreenRaster> raster) {
    if (!raster) throw std::invalid_argument("an offscreen raster result needs a raster");
    std::variant<std::unique_ptr<OffscreenRaster>, Refusal> state(std::move(raster));
    OffscreenRasterCreateResult result(std::move(state));
    return result;
}

OffscreenRasterCreateResult OffscreenRasterCreateResult::refused(OffscreenRasterError error, std::string detail) {
    if (error == OffscreenRasterError::none) throw std::invalid_argument("a refusal needs its error");
    Refusal refusal{};
    refusal.error = error;
    refusal.detail = std::move(detail);
    std::variant<std::unique_ptr<OffscreenRaster>, Refusal> state(std::move(refusal));
    OffscreenRasterCreateResult result(std::move(state));
    return result;
}

bool OffscreenRasterCreateResult::succeeded() const noexcept {
    const bool made = std::holds_alternative<std::unique_ptr<OffscreenRaster>>(state_);
    return made;
}

std::unique_ptr<OffscreenRaster> OffscreenRasterCreateResult::take_raster() noexcept {
    std::unique_ptr<OffscreenRaster>* const held = std::get_if<std::unique_ptr<OffscreenRaster>>(&state_);
    std::unique_ptr<OffscreenRaster> taken{};
    if (held != nullptr) taken = std::move(*held);
    return taken;
}

OffscreenRasterError OffscreenRasterCreateResult::error() const noexcept {
    const Refusal* const refusal = std::get_if<Refusal>(&state_);
    if (refusal == nullptr) return OffscreenRasterError::none;
    return (*refusal).error;
}

std::string OffscreenRasterCreateResult::detail() const {
    const Refusal* const refusal = std::get_if<Refusal>(&state_);
    std::string text{};
    if (refusal != nullptr) text = (*refusal).detail;
    return text;
}

// -- the raster ---------------------------------------------------------------------

OffscreenRaster::OffscreenRaster(std::unique_ptr<Parts> parts) : parts_(std::move(parts)) {}

OffscreenRaster::~OffscreenRaster() {
    // A window still attached would keep the raster's address as its
    // painter: a defect in the caller, refused loudly rather than left to
    // a later use after free.
    if (attachments_ != 0U) {
        std::fputs("GUI.Forms: an OffscreenRaster was destroyed while a window was attached to it\n", stderr);
        std::terminate();
    }
}

const std::filesystem::path& OffscreenRaster::font_directory() const noexcept {
    return (*parts_).directory;
}

namespace detail {

OffscreenRasterCreateResult OffscreenRasterAccess::make(std::shared_ptr<BundledFontSet> fonts,
                                                        std::filesystem::path directory) {
    std::unique_ptr<OffscreenRaster::Parts> parts = std::make_unique<OffscreenRaster::Parts>();
    (*parts).fonts = std::move(fonts);
    (*parts).directory = std::move(directory);
    // The first surface, as the specimen sheet and the hosts make it
    // before registering faces; captures make framebuffers of their own.
    const Size first_surface{64.0, 64.0};
    if (!(*parts).raster.resize(first_surface, 1.0)) {
        OffscreenRasterCreateResult refusal = OffscreenRasterCreateResult::refused(
            OffscreenRasterError::raster_refused, "the CPU raster could not make its first surface");
        return refusal;
    }
    if (!(*parts).fonts || !(*parts).raster.register_bundled_fonts((*parts).fonts)) {
        OffscreenRasterCreateResult refusal = OffscreenRasterCreateResult::refused(
            OffscreenRasterError::fonts_refused, "the raster refused the bundled faces in " + (*parts).directory.string());
        return refusal;
    }
    std::unique_ptr<OffscreenRaster> raster(new OffscreenRaster(std::move(parts)));
    OffscreenRasterCreateResult result = OffscreenRasterCreateResult::created(std::move(raster));
    return result;
}

} // namespace detail

// -- the attachment and its captures ---------------------------------------------

namespace {

// Puts back the window's scale and presentation overrides when a capture
// ends, an exception included.
class CaptureSettingsScope final {
public:
    CaptureSettingsScope(Window& window, const OffscreenCaptureOptions& options)
        : window_(window), scale_(window.scale()) {
        const PresentationOverrides* const current = window.presentation_overrides();
        if (current != nullptr) overrides_ = *current;
        PresentationOverrides applied = overrides_;
        if (options.colour_scheme.has_value()) applied.colour_scheme = options.colour_scheme;
        if (options.high_contrast.has_value()) applied.high_contrast = options.high_contrast;
        changed_overrides_ = !(applied == overrides_);
        if (changed_overrides_) window.set_presentation_overrides(applied);
        changed_scale_ = options.scale != scale_;
        if (changed_scale_) window.set_scale(options.scale);
    }
    ~CaptureSettingsScope() {
        if (changed_scale_) window_.set_scale(scale_);
        if (changed_overrides_) window_.set_presentation_overrides(overrides_);
    }
    CaptureSettingsScope(const CaptureSettingsScope&) = delete;
    CaptureSettingsScope& operator=(const CaptureSettingsScope&) = delete;

private:
    Window& window_;
    double scale_{};
    PresentationOverrides overrides_{};
    bool changed_overrides_{};
    bool changed_scale_{};
};

bool accepted_scale(double scale) noexcept {
    const bool accepted = std::isfinite(scale) && scale > 0.0 && scale <= 8.0;
    return accepted;
}

} // namespace

OffscreenAttachment::OffscreenAttachment(OffscreenRaster& raster, Window& window)
    : raster_(raster), window_(window) {
    window.verify_access("OffscreenAttachment");
    previous_provider_ = window.text_metrics_provider();
    render::SkiaRaster& painter = (*raster.parts_).raster;
    window.set_text_metrics_provider(&painter);
    window.set_framebuffer_painter(&painter);
    raster.attachments_ = raster.attachments_ + 1U;
}

OffscreenAttachment::~OffscreenAttachment() {
    window_.set_framebuffer_painter(nullptr);
    window_.set_text_metrics_provider(previous_provider_);
    raster_.attachments_ = raster_.attachments_ - 1U;
}

std::optional<WindowCapture> OffscreenAttachment::capture_window(const OffscreenCaptureOptions& options) {
    if (!accepted_scale(options.scale)) return std::nullopt;
    const CaptureSettingsScope settings(window_, options);
    window_.perform_layout();
    WindowCaptureOptions request{};
    request.include_overlays = options.include_overlays;
    request.scale = options.scale;
    std::optional<WindowCapture> capture = window_.capture_window(request);
    return capture;
}

std::optional<WindowCapture> OffscreenAttachment::capture_control(const Control::Ptr& control,
                                                                  const OffscreenCaptureOptions& options) {
    if (!accepted_scale(options.scale)) return std::nullopt;
    const CaptureSettingsScope settings(window_, options);
    window_.perform_layout();
    ControlCaptureOptions request{};
    request.scale = options.scale;
    request.margin = options.margin;
    request.include_backplane = options.include_backplane;
    std::optional<WindowCapture> capture = window_.capture_control(control, request);
    return capture;
}

} // namespace gui_forms
