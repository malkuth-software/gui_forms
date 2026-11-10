// Included only in the dedicated test translation unit. No installed header or
// production host exports these controls or private inspection operations.
class DibFixtureControl final : public Control {
public:
    DibFixtureControl() : Control(StableId("dib-fixture")) {}
    Color color{0x22, 0x44, 0x66, 0xff};
    bool fail{};
    bool revoke_epoch{};
    std::shared_ptr<LiveSurface> surface{};
protected:
    void on_paint(Painter& painter, Rect damage) override {
        painter.fill_rect(damage, color);
        if (surface) painter.draw_live_surface(surface, client_rectangle(), 1.0);
        if (fail) throw std::runtime_error("fixture callback failure");
        if (revoke_epoch) {
            revoke_epoch = false;
            Window& model = *attached_window();
            const double next_scale = model.scale() + 0.25;
            model.set_scale(next_scale);
        }
    }
};

struct DibHostFixture final {
    static void require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }
    // Commits the raster's open frame into its DIB front through a memory DC
    // standing for the window, as one whole-surface frame.
    static void commit_offscreen(WindowsRaster& raster, const int width, const int height) {
        HDC target = CreateCompatibleDC(nullptr);
        require(target != nullptr, "offscreen target created");
        const RECT whole{0, 0, width, height};
        const bool committed = raster.commit_frame(target, whole, PaintReceipt{1U, 1U});
        DeleteDC(target);
        require(committed, "offscreen frame committed");
    }
    // An opaque live frame of one colour, premultiplied BGRA8.
    static void fill_opaque(const std::span<std::byte> bytes, const std::byte blue,
                            const std::byte green, const std::byte red) {
        for (std::size_t index = 0U; index + 3U < bytes.size(); index += 4U) {
            bytes[index] = blue;
            bytes[index + 1U] = green;
            bytes[index + 2U] = red;
            bytes[index + 3U] = std::byte{0xff};
        }
    }
    // A live frame reaches the DIB exactly: the painter draws it, and the
    // copy into the DIB converts nothing (SK2, SK3).
    static void live_format_pixels() {
        constexpr LiveSurfacePixelFormat format = LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
        const std::shared_ptr<LiveSurface> surface = LiveSurface::create(
            {.width = 2U, .height = 2U, .pixel_format = format, .opaque = true});
        require(static_cast<bool>(surface), "format surface allocated");
        LiveSurfaceWriteLease write = (*surface).try_acquire_write();
        require(static_cast<bool>(write), "format write acquired");
        fill_opaque(write.pixels(), std::byte{0x25}, std::byte{0x67}, std::byte{0xd3});
        require(write.publish() != 0U, "format frame published");
        {
            WindowsRaster raster{};
            require(raster.resize({6.0, 4.0}, 1.0), "format DIB allocated");
            require(raster.begin_frame(), "format DIB begun");
            render::SkiaRaster& painter = raster.painter();
            painter.fill_rect({0.0, 0.0, 6.0, 4.0}, Color::rgba(0, 255, 0));
            painter.save();
            painter.clip_rect({2.0, 1.0, 1.0, 2.0});
            painter.draw_live_surface(surface, {1.0, 1.0, 2.0, 2.0}, 1.0);
            painter.restore();
            commit_offscreen(raster, 6, 4);
            const detail::DibFrontView front = raster.frames_.front();
            require(front.pixels[8] == 0xffd36725U && front.pixels[14] == 0xffd36725U,
                    "1:1 DIB copy preserves exact BGRA color and alpha");
            require((front.pixels[7] & 0x00ffffffU) == 0x0000ff00U, "DIB copy respects clip");
        }
        {
            WindowsRaster raster{};
            require(raster.resize({6.0, 4.0}, 1.0), "scaled DIB allocated");
            require(raster.begin_frame(), "scaled DIB begun");
            raster.painter().draw_live_surface(surface, {0.0, 0.0, 6.0, 4.0}, 1.0);
            commit_offscreen(raster, 6, 4);
            const detail::DibFrontView front = raster.frames_.front();
            for (std::size_t index = 0U; index < 24U; ++index) {
                require((front.pixels[index] & 0x00ffffffU) == 0x00d36725U, "a scaled live frame keeps its colour");
            }
        }
    }
    static void request_paint(WindowsHostState& state) {
        InvalidateRect(state.hwnd_, nullptr, FALSE);
        SendMessageW(state.hwnd_, WM_PAINT, 0, 0);
    }
    static void require_front(WindowsHostState& state, detail::DibFrameKey key,
                              const std::vector<std::uint32_t>& pixels) {
        require(detail::same_frame_key(state.raster_.frames_.front_key(), key), "host front identity changed");
        const detail::DibFrontView front = state.raster_.frames_.front();
        require(front.pixels.size() == pixels.size(), "host front extent changed");
        require(std::equal(pixels.begin(), pixels.end(), front.pixels.begin()), "host front pixels changed");
    }
    static void exercise(WindowsHostState& state, DibFixtureControl& control, const LiveSurfacePixelFormat format) {
        require(!state.raster_.frames_.has_front(), "a front exists before the first paint");
        request_paint(state);
        const PaintLeaseSnapshot initial = (*state.model_).paint_lease_snapshot();
        require(initial.presented_revision != 0, "initial Window receipt not accepted");
        require(state.raster_.matches_front(initial), "initial front/model mismatch");
        const detail::DibFrameKey first = state.raster_.frames_.front_key();
        const detail::DibFrontView view = state.raster_.frames_.front();
        const std::vector<std::uint32_t> old_pixels(view.pixels.begin(), view.pixels.end());

        control.color = {0xaa, 0xbb, 0xcc, 0xff};
        control.invalidate(Dirty::paint);
        state.collect_damage();
        state.raster_.frames_.inject_failure(detail::DibFrameFault::commit_flush);
        request_paint(state);
        require_front(state, first, old_pixels);
        const PaintLeaseSnapshot failed = (*state.model_).paint_lease_snapshot();
        require(failed.presented_revision == initial.presented_revision, "failed frame acknowledged");
        require(!state.pending_damage_.empty(), "failed damage lost");
        request_paint(state);
        require_front(state, first, old_pixels);
        require((*state.model_).paint_lease_snapshot().leases_started == failed.leases_started,
                "recovery exposure unexpectedly repainted");
        request_paint(state);
        require(state.raster_.matches_front((*state.model_).paint_lease_snapshot()), "recovery commit mismatch");

        control.fail = true;
        control.invalidate(Dirty::paint);
        state.collect_damage();
        const detail::DibFrameKey before_throw = state.raster_.frames_.front_key();
        request_paint(state);
        require(detail::same_frame_key(state.raster_.frames_.front_key(), before_throw), "callback failure committed");
        require(state.native_callback_faults_ == 1, "callback failure not contained");
        control.fail = false;
        request_paint(state);
        request_paint(state);

        control.revoke_epoch = true;
        control.invalidate(Dirty::paint);
        state.collect_damage();
        const detail::DibFrameKey before_revoke = state.raster_.frames_.front_key();
        request_paint(state);
        require(detail::same_frame_key(state.raster_.frames_.front_key(), before_revoke), "revoked epoch committed");
        require((*state.model_).paint_lease_snapshot().surface_epoch != before_revoke.epoch,
                "fixture did not revoke the model epoch");
        (*state.model_).set_scale(state.scale_);
        request_paint(state);
        request_paint(state);

        std::shared_ptr<LiveSurface> surface = LiveSurface::create({.width = 4U, .height = 4U, .pixel_format = format,
            .buffer_count = 2U, .opaque = true});
        require(static_cast<bool>(surface), "live surface allocation");
        LiveSurfaceWriteLease write = (*surface).try_acquire_write();
        require(static_cast<bool>(write), "live write lease");
        const std::span<std::byte> bytes = write.pixels();
        fill_opaque(bytes, std::byte{0x55}, std::byte{0x55}, std::byte{0x55});
        const std::uint64_t published_generation = write.publish();
        require(published_generation != 0, "live publish");
        control.surface = surface;
        control.invalidate(Dirty::paint);
        state.collect_damage();
        request_paint(state);
        const Control::Ptr root = (*state.model_).root();
        const bool live_registered = (*state.model_).queue_live_surface_presentation(root, surface);
        require(live_registered, "live registration");
        const detail::DibFrameKey live_key = state.raster_.frames_.front_key();
        state.present_live_surface_updates();
        require(state.live_updates_presented_ != 0, "live host presentation missing");
        require(detail::same_frame_key(state.raster_.frames_.front_key(), live_key), "live update changed receipt");
        const detail::DibFrontView live_front = state.raster_.frames_.front();
        require((live_front.pixels[0] & 0x00ffffffU) == 0x00555555U, "live glyph-free raster pixels missing");
        const std::vector<std::uint32_t> live_pixels(live_front.pixels.begin(), live_front.pixels.end());
        // With two buffers, retaining the old native read lease prevents the
        // second producer rotation from reacquiring that old buffer.
        for (int rotation = 0; rotation < 2; ++rotation) {
            LiveSurfaceWriteLease recycled = (*surface).try_acquire_write();
            require(static_cast<bool>(recycled), "live native read lease retained");
            const std::span<std::byte> recycled_bytes = recycled.pixels();
            fill_opaque(recycled_bytes, std::byte{0x77}, std::byte{0x77}, std::byte{0x77});
            const std::uint64_t recycled_generation = recycled.publish();
            require(recycled_generation != 0, "recycled live publish");
        }
        state.raster_.frames_.inject_failure(detail::DibFrameFault::present);
        state.present_live_surface_updates();
        require_front(state, live_key, live_pixels);
        require(state.live_updates_failed_ != 0, "failed live presentation not counted");
        request_paint(state);
        require_front(state, live_key, live_pixels);
        request_paint(state);
        require(state.pending_damage_.empty(), "same-revision recovery left permanent damage");
        const detail::DibFrontView recovered_live = state.raster_.frames_.front();
        require((recovered_live.pixels[0] & 0x00ffffffU) == 0x00777777U,
                "normal recovery lost the newest live generation");

        control.invalidate(Dirty::paint);
        state.collect_damage();
        const std::uint64_t sampled = state.live_updates_sampled_;
        state.present_live_surface_updates();
        require(state.live_updates_sampled_ == sampled, "stale-front live placement sampled");
        request_paint(state);

        const detail::DibFrameKey before_resize = state.raster_.frames_.front_key();
        const detail::DibFrontView before_extent = state.raster_.frames_.front();
        const int old_width = before_extent.width;
        const int old_height = before_extent.height;
        SetWindowPos(state.hwnd_, nullptr, 0, 0, 120, 100,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        state.resize();
        state.raster_.frames_.inject_failure(detail::DibFrameFault::allocation);
        request_paint(state);
        require(detail::same_frame_key(state.raster_.frames_.front_key(), before_resize), "failed resize relabelled front");
        const detail::DibFrontView failed_resize = state.raster_.frames_.front();
        require(failed_resize.width == old_width && failed_resize.height == old_height, "failed resize changed old extent");
        request_paint(state);
        request_paint(state);
        require(state.raster_.matches_front((*state.model_).paint_lease_snapshot()), "resized receipt mismatch");
        const detail::DibFrameKey before_empty = state.raster_.frames_.front_key();
        const bool empty_size_accepted = state.raster_.resize({}, state.scale_);
        require(empty_size_accepted, "zero size accepted");
        const bool empty_frame_begun = state.raster_.begin_frame();
        require(!empty_frame_begun, "zero size allocated candidate");
        require(detail::same_frame_key(state.raster_.frames_.front_key(), before_empty), "zero size relabelled front");
        state.resize();
        SendMessageW(state.hwnd_, WM_CLOSE, 0, 0);
        require(state.hwnd_ == nullptr, "owned native window did not close");
    }
    // A window frame keeps alpha while its backdrop grant is not opaque
    // (stage T5, C2): the window's clear material presents clear, where
    // the DIB otherwise presents every pixel opaque. The frame is painted
    // over a stale opaque colour, standing for the previous frame.
    static std::uint32_t frame_alpha_pixel(Window& model, bool kept) {
        WindowsRaster raster{};
        require(raster.resize({40.0, 30.0}, 1.0), "frame alpha DIB allocated");
        raster.set_frame_alpha(kept);
        require(raster.begin_frame(), "frame alpha DIB begun");
        raster.painter().fill_rect({0.0, 0.0, 40.0, 30.0}, Color::rgba(250, 0, 0, 255));
        require(model.paint(raster.painter(), Rect{0.0, 0.0, 40.0, 30.0}).has_value(), "frame alpha window painted");
        commit_offscreen(raster, 40, 30);
        const std::uint32_t pixel = raster.frames_.front().pixels[15U * 40U + 20U];
        return pixel;
    }
    static void frame_alpha() {
        const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
        SurfaceMaterial tint{};
        tint.fills.clear();
        tint.fills.push_back(MaterialFillLayer::solid(Color::rgba(40, 80, 120, 128)));
        ControlRoleRecipes& recipes = (*held).roles[static_cast<std::size_t>(ControlVisualRole::window)];
        for (ControlVisualRecipe& recipe : recipes.ordinary) recipe.material = tint;
        const std::shared_ptr<Control> root = make_control<Control>(StableId("frame-alpha"));
        Window model(root, {40.0, 30.0});
        model.set_theme(Theme::create(*held));
        require(model.declare_non_opaque(), "frame alpha window declared");
        CompositorState state{};
        state.generation = CompositorGeneration{1U};
        state.compositing = CompositorPresence::present;
        state.support[static_cast<std::size_t>(WindowBackdrop::transparent)] = WindowBackdropSupport::supported;
        model.set_compositor_state(state, false);
        const WindowBackdropGrant grant =
            model.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent));
        require(grant.granted == WindowBackdrop::transparent, "frame alpha window granted transparent");
        // Premultiplied BGRA: the tint is (20, 40, 60) at alpha 128.
        const std::uint32_t clear = frame_alpha_pixel(model, true);
        const unsigned alpha = clear >> 24U;
        const unsigned red = (clear >> 16U) & 255U;
        const unsigned blue = clear & 255U;
        require(alpha + 1U >= 128U && alpha <= 129U && red + 2U >= 20U && red <= 22U && blue + 2U >= 60U &&
                    blue <= 62U,
                "a frame that keeps alpha presents the window's translucent material, premultiplied");
        const std::uint32_t opaque = frame_alpha_pixel(model, false);
        require((opaque >> 24U) == 255U, "a frame that keeps no alpha presents every pixel opaque");
    }

    // Sends one automation command to `window` as a harness would; the
    // window procedure's answer (TRUE when the command was accepted).
    static LRESULT send_automation(HWND window, const char* command) {
        COPYDATASTRUCT data{};
        data.dwData = 0x47464131U;
        data.cbData = static_cast<DWORD>(std::strlen(command) + 1U);
        data.lpData = const_cast<char*>(command);
        const LRESULT answer = SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
        return answer;
    }

    // The automation channel is off by default: a WM_COPYDATA command is
    // ignored; with automation_enabled it is accepted.
    static void automation_gate(const wchar_t* class_name, HINSTANCE instance) {
        for (int enabled = 0; enabled < 2; ++enabled) {
            std::shared_ptr<DibFixtureControl> control = std::make_shared<DibFixtureControl>();
            std::unique_ptr<Window> model = std::make_unique<Window>(control, Size{80, 60});
            WindowsHostOptions options{};
            options.quit_thread_on_close = false;
            options.print_metrics_on_close = false;
            options.automation_enabled = enabled != 0;
            WindowsHostState state(std::move(model), options);
            HWND window = CreateWindowExW(WS_EX_NOACTIVATE, class_name, L"Automation fixture", WS_POPUP, 0, 0,
                                          80, 60, nullptr, nullptr, instance, &state);
            require(window != nullptr, "automation fixture native creation");
            const bool initialized = state.initialize(window);
            require(initialized, "automation fixture host initialization");
            const LRESULT answer = send_automation(window, "GUI.Forms.Automation/1 snapshot");
            if (enabled == 0) {
                require(answer == FALSE, "automation off by default: a WM_COPYDATA command is ignored");
            } else {
                require(answer == TRUE, "automation enabled: a WM_COPYDATA command is accepted");
            }
            DestroyWindow(window);
        }
    }

    static void run() {
        const HRESULT com_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        require(SUCCEEDED(com_status), "fixture COM setup");
        try {
            live_format_pixels();
            frame_alpha();
            WNDCLASSEXW native_class{};
            native_class.cbSize = sizeof(native_class);
            native_class.lpfnWndProc = window_procedure;
            native_class.hInstance = GetModuleHandleW(nullptr);
            native_class.lpszClassName = L"GUIForms.DibFixture.v1";
            const ATOM registered_class = RegisterClassExW(&native_class);
            require(registered_class != 0, "fixture class registration");
            automation_gate(native_class.lpszClassName, native_class.hInstance);
            {
                constexpr LiveSurfacePixelFormat format = LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
                std::shared_ptr<DibFixtureControl> control = std::make_shared<DibFixtureControl>();
                std::unique_ptr<Window> model = std::make_unique<Window>(control, Size{80, 60});
                WindowsHostOptions options{};
                options.quit_thread_on_close = false;
                WindowsHostState state(std::move(model), options);
                HWND window = CreateWindowExW(WS_EX_NOACTIVATE, native_class.lpszClassName,
                    L"DIB fixture", WS_POPUP, 0, 0, 80, 60, nullptr, nullptr,
                    native_class.hInstance, &state);
                require(window != nullptr, "fixture native creation");
                const bool initialized = state.initialize(window);
                require(initialized, "fixture host initialization");
                exercise(state, *control, format);
            }
            const BOOL class_released = UnregisterClassW(native_class.lpszClassName, native_class.hInstance);
            require(class_released != FALSE,
                    "fixture class release");
        } catch (...) {
            CoUninitialize();
            throw;
        }
        CoUninitialize();
    }
};
