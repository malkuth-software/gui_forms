#include "form_preview.hpp"

#include "winforms_project.hpp"

#include "../../src/core/form_language/form_loader/form_window_loader.hpp"

#include "gui_forms/application.hpp"
#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace gui_forms::winforms_converter {

namespace {

// A PNG's width and height from its IHDR chunk; zero when `bytes` is not one.
struct PngSize final {
    std::uint32_t width{};
    std::uint32_t height{};
};

[[nodiscard]] std::uint32_t big_endian(std::string_view bytes, std::size_t at) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value = (value << 8U) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + index]));
    }
    return value;
}

[[nodiscard]] PngSize png_size(std::string_view bytes) noexcept {
    PngSize size{};
    constexpr std::string_view signature("\x89PNG\r\n\x1a\n", 8U);
    if (bytes.size() < 24U || bytes.substr(0U, 8U) != signature || bytes.substr(12U, 4U) != "IHDR") return size;
    size.width = big_endian(bytes, 16U);
    size.height = big_endian(bytes, 20U);
    return size;
}

[[nodiscard]] bool name_end(char byte) noexcept {
    const bool ends = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '/' || byte == '>';
    return ends;
}

[[nodiscard]] std::unique_ptr<Application> preview_application() {
    install_built_in_effect_registry();
    ApplicationConfig config{};
    config.theme.base = ThemeBase::windows_professional;
    config.theme.effects = &built_in_effect_registry();
    ApplicationCreateResult created = Application::create(std::move(config));
    std::unique_ptr<Application> application = std::move(created.application);
    return application;
}

[[nodiscard]] ConverterToolReply preview_refused(std::string reason) {
    ConverterToolReply reply{};
    reply.text = reason + "\n";
    reply.json = "{\"problem\":" + json_string(reason) + "}";
    return reply;
}

} // namespace

std::string without_bindings(std::string_view document) {
    std::string blanked(document);
    std::size_t found = blanked.find("<bind");
    while (found != std::string::npos) {
        const std::size_t after = found + 5U;
        if (after >= blanked.size() || !name_end(blanked[after])) {
            found = blanked.find("<bind", after);
            continue;
        }
        std::size_t end = blanked.find('>', after);
        if (end == std::string::npos) break;
        // `<bind ...></bind>` as well as `<bind .../>`.
        if (blanked[end - 1U] != '/') {
            const std::size_t close = blanked.find("</bind>", end);
            if (close != std::string::npos) end = close + 6U;
        }
        for (std::size_t index = found; index <= end; ++index) {
            if (blanked[index] != '\n') blanked[index] = ' ';
        }
        found = blanked.find("<bind", end + 1U);
    }
    return blanked;
}

ConverterToolReply run_preview(const PreviewRequest& request, const form_language::FormSchema& schema) {
    if (!(request.scale > 0.0 && request.scale <= 8.0)) return preview_refused("scale must be in (0, 8]");
    std::unique_ptr<Application> made_application{};
    if (Application::current() == nullptr) {
        made_application = preview_application();
        if (made_application == nullptr) return preview_refused("the preview application was refused");
    }
    OffscreenRasterCreateResult made = OffscreenRaster::create();
    if (!made.succeeded()) return preview_refused("no raster: " + made.detail());
    // Declared before the window and the attachment, which end first.
    const std::unique_ptr<OffscreenRaster> raster = made.take_raster();
    const std::string file = request.file.empty() ? std::string("form.form") : request.file;
    const std::string document = without_bindings(request.document);
    const form_language::FormSources sources{};
    theme_language::ThemeDiagnosticList diagnostics{};
    form_language::LoadedWindowResult loaded =
        form_language::load_window(file, document, schema, sources, diagnostics);
    if (!loaded) {
        ConverterToolReply reply{};
        reply.text = "the document did not load\n";
        std::string listed = "[";
        for (std::size_t index = 0U; index < diagnostics.records().size(); ++index) {
            const std::string line = theme_diagnostic_line(diagnostics.records()[index]);
            reply.text.append(line + "\n");
            if (index != 0U) listed.push_back(',');
            listed.append(json_string(line));
        }
        reply.json = "{\"problem\":\"the document did not load\",\"diagnostics\":" + listed + "]}";
        return reply;
    }
    Window& window = *loaded.window;
    window.set_active(true);
    std::optional<WindowCapture> capture{};
    {
        OffscreenAttachment attached(*raster, window);
        window.perform_layout();
        OffscreenCaptureOptions options{};
        options.scale = request.scale;
        options.colour_scheme.set(ColourScheme::light);
        capture = attached.capture_window(options);
    }
    std::vector<std::byte> png{};
    if (!capture.has_value() || !encode_window_capture_png(*capture, png)) {
        return preview_refused("the capture or its PNG encoding was refused");
    }
    ConversionDiagnosticList written{};
    const std::string_view bytes(reinterpret_cast<const char*>(png.data()), png.size());
    if (!write_text_file(request.output_png, bytes, written)) return preview_refused("cannot write " + request.output_png);
    ConverterToolReply reply{};
    reply.succeeded = true;
    reply.text = "preview " + request.output_png + ": " + std::to_string((*capture).pixel_width) + "x" +
                 std::to_string((*capture).pixel_height) + " pixels at scale " + std::to_string(request.scale);
    reply.json = "{\"preview\":{\"path\":" + json_string(request.output_png) + ",\"width\":" +
                 std::to_string((*capture).pixel_width) + ",\"height\":" + std::to_string((*capture).pixel_height) +
                 "}";
    if (!request.original_png.empty()) {
        std::string original{};
        ConversionDiagnosticList read{};
        const PngSize size = read_source_file(request.original_png, original, read) ? png_size(original) : PngSize{};
        reply.text.append("; original " + request.original_png + ": " + std::to_string(size.width) + "x" +
                          std::to_string(size.height) + " pixels");
        reply.json.append(",\"original\":{\"path\":" + json_string(request.original_png) + ",\"width\":" +
                          std::to_string(size.width) + ",\"height\":" + std::to_string(size.height) + "}");
    }
    reply.text.append("\n");
    reply.json.append("}");
    return reply;
}

} // namespace gui_forms::winforms_converter
