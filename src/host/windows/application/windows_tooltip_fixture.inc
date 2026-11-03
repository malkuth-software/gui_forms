// Dedicated native tooltip test seam; excluded from production host builds.
struct TooltipHostFixture final {
    static void require(const bool condition, const char* const message) {
        if (!condition) throw std::runtime_error(message);
    }

    static void verify_pixels(const TooltipPopup& popup, const double scale) {
        const std::size_t width = static_cast<std::size_t>(popup.width);
        const std::size_t height = static_cast<std::size_t>(popup.height);
        require(width >= 3U && height >= 3U, "tooltip has an interior");
        require(popup.pixels.size() == width * height, "tooltip pixel extent");
        require(static_cast<double>(width) <= std::ceil(376.0 * scale), "tooltip wrap width plus padding");
        constexpr std::uint32_t border = 0xff697480U;
        constexpr std::uint32_t face = 0xfffffff0U;
        for (std::size_t x = 0; x < width; ++x) {
            require(popup.pixels[x] == border, "tooltip top border reaches every device pixel");
            require(popup.pixels[(height - 1U) * width + x] == border,
                    "tooltip bottom border reaches every device pixel");
        }
        for (std::size_t y = 0; y < height; ++y) {
            require(popup.pixels[y * width] == border, "tooltip left border reaches every device pixel");
            require(popup.pixels[y * width + width - 1U] == border,
                    "tooltip right border reaches every device pixel");
        }
        require(popup.pixels[width + 1U] == face, "tooltip top left padding");
        require(popup.pixels[2U * width - 2U] == face, "tooltip border is one device pixel wide");
        std::size_t ink = 0;
        for (std::size_t y = 1; y + 1U < height; ++y) {
            for (std::size_t x = 1; x + 1U < width; ++x) {
                const std::uint32_t pixel = popup.pixels[y * width + x];
                require((pixel >> 24U) == 255U, "tooltip has no unpainted pixels");
                if (pixel != face) ++ink;
            }
        }
        require(ink > 0U, "tooltip has visible glyph pixels");
    }

    static void run(const std::filesystem::path& directory) {
        const std::shared_ptr<gui_forms::detail::BundledFontSet> fonts = load_bundled_fonts(directory);
        require(fonts != nullptr, "tooltip fixture fonts available");
        const std::shared_ptr<Control> root = make_control<Control>(StableId("tooltip.fixture"));
        WindowsHostState state(std::make_unique<Window>(root, Size{800.0, 600.0}), {});
        state.hwnd_ = CreateWindowExW(0, L"STATIC", L"Tooltip fixture owner", WS_OVERLAPPEDWINDOW,
            0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(state.hwnd_ != nullptr, "tooltip native owner created");
        state.raster_.set_bundled_fonts(fonts);
        for (const double scale : {1.0, 1.1, 1.25, 1.5, 2.0}) {
            state.scale_ = scale;
            require(state.raster_.resize({800.0, 600.0}, scale), "tooltip metrics surface resized");
            HostTooltipRequest request{.text = "Ag", .anchor = {12.0, 12.0}};
            HostServiceStatus status = state.show_tooltip(request);
            require(status.accepted(), "short tooltip shown");
            verify_pixels(state.tooltip_, scale);
            const int short_height = state.tooltip_.height;
            const HWND previous = state.tooltip_.window;
            state.hide_tooltip();
            require(IsWindow(previous) == FALSE && state.tooltip_.pixels.empty(), "tooltip hides and releases pixels");
            request.text = "Native tooltip: Latin Файл 日本語 العربية עברית हिन्दी বাংলা ਪੰਜਾਬੀ. "
                "This deliberately long sentence must wrap at 360 logical pixels and keep its border and padding.";
            status = state.show_tooltip(request);
            require(status.accepted(), "wrapped multilingual tooltip shown");
            verify_pixels(state.tooltip_, scale);
            require(state.tooltip_.height > short_height, "long tooltip wraps onto additional lines");
            RECT client{};
            require(GetClientRect(state.tooltip_.window, &client) != FALSE, "native tooltip client bounds");
            require(client.right == state.tooltip_.width && client.bottom == state.tooltip_.height,
                    "native popup and painted buffer extents agree");
            std::printf("tooltip scale=%.1f pixels=%dx%d borders=exact wrapping=passed teardown=passed\n",
                scale, state.tooltip_.width, state.tooltip_.height);
            state.hide_tooltip();
        }
    }
};
