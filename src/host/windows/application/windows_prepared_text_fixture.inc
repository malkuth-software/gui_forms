// Dedicated hidden-window test seam, never enabled in production builds.
struct PreparedFixtureClass final {
    HINSTANCE instance{};
    const wchar_t* name{};
    bool registered{};
    ~PreparedFixtureClass() { if (registered) UnregisterClassW(name, instance); }
};

class PreparedFixtureControl final : public Control {
public:
    PreparedFixtureControl() : Control(StableId("prepared-fixture")) {}
    PreparedTextLayout layout{};
    Color background{255, 255, 255, 255};
protected:
    void on_paint(Painter& painter, const Rect damage) override {
        painter.fill_rect(damage, background);
        if (!layout.empty()) {
            const PreparedTextPaintResult result = painter.draw_prepared_text(layout, layout.authority(), {8, 32}, {0, 0, 0, 255});
            if (result.status != PreparedTextStatus::success) throw gui_forms::detail::PreparedTextPaintFailure(result.status);
        }
    }
};

struct PreparedHostFixture final {
    static void request_paint(WindowsHostState& state) {
        InvalidateRect(state.hwnd_, nullptr, FALSE);
        SendMessageW(state.hwnd_, WM_PAINT, 0, 0);
    }
    static void verify_front(WindowsHostState& state, const detail::DibFrameKey identity,
        const std::span<const std::uint32_t> pixels, const std::uint64_t presented) {
        prepared_test::require(detail::same_frame_key(state.raster_.frames_.front_key(), identity), "prepared failure changed front identity");
        const detail::DibFrontView current = state.raster_.frames_.front();
        prepared_test::require(current.pixels.size() == pixels.size(), "prepared failure changed extent");
        const bool equal = std::equal(pixels.begin(), pixels.end(), current.pixels.begin());
        prepared_test::require(equal, "prepared failure changed front pixels");
        prepared_test::require((*state.model_).paint_lease_snapshot().presented_revision == presented, "prepared failure acknowledged receipt");
    }
    static void exercise(WindowsHostState& state, PreparedFixtureControl& control,
        PreparedTextService& service, PreparedTextSession& session, const EncodedFontLease& bank) {
        const PreparedTextKey key = prepared_test::make_key(service, bank, "Prepared", 16, state.scale_);
        prepared_test::prepare(service, session, key, "Prepared", control.layout);
        control.invalidate(Dirty::paint);
        state.collect_damage();
        request_paint(state);
        const PaintLeaseSnapshot first_receipt = (*state.model_).paint_lease_snapshot();
        prepared_test::require(first_receipt.presented_revision != 0 && state.raster_.matches_front(first_receipt), "prepared initial frame not presented");
        const detail::DibFrameKey first_key = state.raster_.frames_.front_key();
        const detail::DibFrontView front = state.raster_.frames_.front();
        const std::vector<std::uint32_t> first_pixels(front.pixels.begin(), front.pixels.end());
        bool ink = false;
        for (std::size_t index = 0; index < first_pixels.size(); ++index) {
            if ((first_pixels[index] & 0xffffffU) != 0xffffffU) ink = true;
        }
        prepared_test::require(ink, "prepared initial frame has no ink");

        session.cancel();
        // Force exposure replay without dirtying the control: the saved command
        // must reject stale authority even though no callback rebuild occurs.
        state.pending_damage_.add({0, 0, 160, 64});
        request_paint(state);
        verify_front(state, first_key, first_pixels, first_receipt.presented_revision);
        request_paint(state); // exposure-only recovery, never relabel old pixels
        verify_front(state, first_key, first_pixels, first_receipt.presented_revision);

        prepared_test::prepare(service, session, key, "Prepared", control.layout);
        control.background = {240, 220, 200, 255};
        control.invalidate(Dirty::paint);
        state.collect_damage();
        state.raster_.frames_.inject_failure(detail::DibFrameFault::commit_flush);
        request_paint(state);
        verify_front(state, first_key, first_pixels, first_receipt.presented_revision);
        request_paint(state);
        request_paint(state);
        const PaintLeaseSnapshot recovered = (*state.model_).paint_lease_snapshot();
        prepared_test::require(recovered.presented_revision > first_receipt.presented_revision &&
            state.raster_.matches_front(recovered), "prepared replacement did not recover");

        const detail::DibFrameKey before_direct = state.raster_.frames_.front_key();
        const bool begun = state.raster_.begin_frame();
        prepared_test::require(begun, "direct candidate begins");
        const PreparedTextPaintResult staged = state.raster_.painter().draw_prepared_text(control.layout, control.layout.authority(), {8, 32}, {0, 0, 0, 255});
        prepared_test::require(staged.disposition == PreparedTextPaintDisposition::staged, "the raster reports staged, not presented");
        session.cancel();
        HDC dc = GetDC(state.hwnd_);
        prepared_test::require(dc != nullptr, "direct commit DC");
        const RECT damage{0, 0, state.raster_.width_, state.raster_.height_};
        const bool committed = state.raster_.commit_frame(dc, damage, {recovered.presented_revision + 1U, recovered.surface_epoch});
        ReleaseDC(state.hwnd_, dc);
        prepared_test::require(!committed && detail::same_frame_key(state.raster_.frames_.front_key(), before_direct), "revoked staged frame committed");
        SendMessageW(state.hwnd_, WM_CLOSE, 0, 0);
        prepared_test::require(state.hwnd_ == nullptr, "prepared native window closes");
    }
    static void run(const std::span<const std::byte> font_bytes) {
        const HRESULT com_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        prepared_test::require(SUCCEEDED(com_status), "prepared fixture COM");
        try {
            WNDCLASSEXW native_class{};
            native_class.cbSize = sizeof(native_class);
            native_class.lpfnWndProc = window_procedure;
            native_class.hInstance = GetModuleHandleW(nullptr);
            native_class.lpszClassName = L"GUIForms.PreparedFixture.v1";
            PreparedFixtureClass class_owner{native_class.hInstance, native_class.lpszClassName, false};
            const ATOM registered = RegisterClassExW(&native_class);
            class_owner.registered = registered != 0;
            prepared_test::require(registered != 0, "prepared fixture class");
            {
                PreparedTextService service{};
                EncodedFontLease bank = prepared_test::make_bank(service, font_bytes);
                std::unique_ptr<PreparedTextSession> session{};
                const PreparedTextStatus opened = service.open_session(bank, nullptr, session);
                prepared_test::require(opened == PreparedTextStatus::success, "prepared fixture session");
                std::shared_ptr<PreparedFixtureControl> control = std::make_shared<PreparedFixtureControl>();
                std::unique_ptr<Window> model = std::make_unique<Window>(control, Size{160, 64});
                WindowsHostOptions options{};
                options.quit_thread_on_close = false;
                WindowsHostState state(std::move(model), options);
                HWND window = CreateWindowExW(WS_EX_NOACTIVATE, native_class.lpszClassName,
                    L"Prepared fixture", WS_POPUP, 0, 0, 160, 64, nullptr, nullptr, native_class.hInstance, &state);
                prepared_test::require(window != nullptr, "prepared fixture native creation");
                const bool initialized = state.initialize(window);
                prepared_test::require(initialized, "prepared fixture initialize");
                exercise(state, *control, service, *session, bank);
            }
            const BOOL released = UnregisterClassW(native_class.lpszClassName, native_class.hInstance);
            if (released != FALSE) class_owner.registered = false;
            prepared_test::require(released != FALSE, "prepared fixture class release");
        } catch (...) { CoUninitialize(); throw; }
        CoUninitialize();
    }
};
