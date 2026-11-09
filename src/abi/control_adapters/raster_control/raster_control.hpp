#pragma once

#include "../support/abi_control_adapter_support.hpp"

namespace gui_forms::abi::detail {

// A compatibility control may be both owner-painted and scrollable. WinForms
// exposes those as independent capabilities through inheritance and styles;
// treating the ABI kind as an exclusive class choice makes legitimate custom
// ScrollableControl subclasses impossible to construct. Keep the raster surface
// on the scrolling substrate. For ordinary owner-painted Control subclasses the
// additional capability is dormant (AutoScroll and both axes default off).
class RasterControl final : public gui_forms::ScrollableControl {
public:
    ~RasterControl() override;

    explicit RasterControl(StableId stable_id, bool input_transparent = false)
        : ScrollableControl(std::move(stable_id)),
          input_transparent_(input_transparent) {
        focusable = !input_transparent_;
    }

    [[nodiscard]] bool hit_test_local(gui_forms::Point point) const override {
        return !input_transparent_ && ScrollableControl::hit_test_local(point);
    }

    [[nodiscard]] gui_forms::Event<const RasterPointerSample&>& pointer_input() noexcept {
        return pointer_input_;
    }

    [[nodiscard]] gui_forms::Event<const RasterKeySample&>& key_input() noexcept {
        return key_input_;
    }

    bool set_png(std::span<const std::byte> encoded) {
        require_mutable();
        if (encoded.empty()) {
            if (window() != nullptr && image_.value != 0) {
                static_cast<void>((*window()).remove_image(image_));
            }
            image_ = {};
            encoded_.clear();
            encoding_ = ImageResourceEncoding::png;
            pixel_width_ = 0;
            pixel_height_ = 0;
            pixel_row_bytes_ = 0;
            invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
            return true;
        }
        const PngValidationResult validation = gui_forms::validate_png(encoded);
        if (!validation) {
            return false;
        }
        encoded_.assign(encoded.begin(), encoded.end());
        encoding_ = ImageResourceEncoding::png;
        pixel_width_ = validation.metadata.width;
        pixel_height_ = validation.metadata.height;
        pixel_row_bytes_ = validation.metadata.source_row_bytes;
        bool replacement_invalidated = false;
        if (window() != nullptr) {
            const bool replacing = image_.value != 0;
            const ImageLoadResult loaded = replacing
                ? (*window()).replace_png(image_, encoded_, *this)
                : (*window()).load_png(encoded_);
            if (!loaded) {
                return false;
            }
            image_ = loaded.image;
            replacement_invalidated = replacing;
        }
        if (!replacement_invalidated) {
            invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
        }
        return true;
    }

    bool set_bgra32_premultiplied(std::uint32_t width, std::uint32_t height,
                                  std::uint64_t row_bytes,
                                  std::span<const std::byte> pixels) {
        require_mutable();
        if (pixels.empty()) {
            return set_png({});
        }
        bool replacement_invalidated = false;
        if (window() != nullptr) {
            const bool replacing = image_.value != 0;
            const bool same_size = replacing && width == pixel_width_ &&
                height == pixel_height_;
            const ImageLoadResult loaded = same_size
                ? (*window()).update_bgra32_premultiplied(
                    image_, width, height, row_bytes, pixels, *this)
                : replacing
                ? (*window()).replace_bgra32_premultiplied(
                    image_, width, height, row_bytes, pixels, *this)
                : (*window()).load_bgra32_premultiplied(
                    width, height, row_bytes, pixels);
            if (!loaded) {
                return false;
            }
            image_ = loaded.image;
            replacement_invalidated = replacing;
            encoded_.clear();
        } else {
            try {
                encoded_.assign(pixels.begin(), pixels.end());
            } catch (const std::bad_alloc&) {
                return false;
            }
        }
        encoding_ = ImageResourceEncoding::bgra32_premultiplied;
        pixel_width_ = width;
        pixel_height_ = height;
        pixel_row_bytes_ = row_bytes;
        if (!replacement_invalidated) {
            invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
        }
        return true;
    }

    void set_live_surface(std::shared_ptr<gui_forms::LiveSurface> surface) {
        require_mutable();
        if (live_surface_ == surface) return;
        disconnect_live_surface_wake();
        live_surface_ = std::move(surface);
        connect_live_surface_wake();
        invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
    }

    void clear_live_surface(const std::shared_ptr<gui_forms::LiveSurface>& surface) {
        require_mutable();
        if (!live_surface_ || (surface && live_surface_ != surface)) {
            return;
        }
        disconnect_live_surface_wake();
        live_surface_.reset();
        invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
    }

    void on_paint(gui_forms::Painter& painter, Rect) override {
        const Rect bounds = committed_arranged_bounds();
        if (live_surface_) {
            const std::uint64_t candidate_generation =
                (*live_surface_).snapshot().published_generation;
            painter.draw_live_surface(live_surface_,
                                      {0.0, 0.0, bounds.width, bounds.height},
                                      1.0);
            // A live wake stays coalesced until the retained paint consumes a
            // candidate, not merely until its dispatcher callback runs. This
            // prevents a fast producer from filling the UI queue while one
            // frame is still waiting to render. If publication raced the
            // paint, latest-frame semantics request exactly one follow-up.
            const std::shared_ptr<LiveWakeState> wake_state = live_wake_state_;
            if (wake_state && (*wake_state).connected.load(
                                  std::memory_order_acquire)) {
                (*wake_state).queued.store(false, std::memory_order_release);
                if ((*live_surface_).snapshot().published_generation !=
                    candidate_generation) {
                    queue_live_surface_paint(
                        std::static_pointer_cast<RasterControl>(
                            shared_from_this()),
                        wake_state);
                }
            }
            return;
        }
        if (image_.value == 0) {
            return;
        }
        painter.draw_image(image_, {0.0, 0.0, bounds.width, bounds.height}, 1.0);
    }

    void on_pointer(gui_forms::PointerEvent& event) override {
        const Rect absolute = absolute_bounds();
        const std::uint32_t kind = pointer_event_kind(event.action);
        pointer_input_.emit({
            kind,
            event.position.x - absolute.x,
            event.position.y - absolute.y,
            event.wheel_delta.y,
            static_cast<std::uint32_t>(event.button),
        });
    }

    void on_key(gui_forms::KeyEvent& event) override {
        key_input_.emit({event.action == gui_forms::KeyAction::down
                             ? GF_EVENT_KEY_DOWN : GF_EVENT_KEY_UP,
                         event.physical_key,
                         static_cast<std::uint32_t>(event.modifiers),
                         event.repeat});
        event.handled = true;
    }

protected:
    void on_attached_to_window() override {
        ScrollableControl::on_attached_to_window();
        if (!encoded_.empty() && image_.value == 0) {
            const ImageLoadResult loaded = encoding_ == ImageResourceEncoding::png
                ? (*window()).load_png(encoded_)
                : (*window()).load_bgra32_premultiplied(
                    pixel_width_, pixel_height_, pixel_row_bytes_, encoded_);
            if (loaded) {
                image_ = loaded.image;
            }
        }
        connect_live_surface_wake();
    }

    void on_detached_from_window() noexcept override {
        disconnect_live_surface_wake();
        if (window() != nullptr && image_.value != 0) {
            if (const std::optional<ImageResourceView> resource = (*window()).image_resources().find(image_);
                resource &&
                (*resource).encoding == ImageResourceEncoding::bgra32_premultiplied) {
                try {
                    encoded_.assign((*resource).encoded.begin(), (*resource).encoded.end());
                } catch (...) {
                    encoded_.clear();
                }
            }
            static_cast<void>((*window()).remove_image(image_));
        }
        image_ = {};
        ScrollableControl::on_detached_from_window();
    }

private:
    struct LiveWakeState final {
        std::atomic<bool> connected{true};
        std::atomic<bool> queued{};
    };

    struct DeferredLiveSurfacePaint final {
        std::weak_ptr<RasterControl> target;
        std::weak_ptr<LiveWakeState> state;

        void operator()() const {
            RasterControl::present_live_surface(target, state);
        }
    };

    struct LiveSurfaceWakeCallback final {
        std::weak_ptr<RasterControl> target;
        std::weak_ptr<LiveWakeState> state;

        void operator()() const noexcept {
            RasterControl::queue_live_surface_paint(target, state);
        }
    };

    static void present_live_surface(
        const std::weak_ptr<RasterControl>& weak_target,
        const std::weak_ptr<LiveWakeState>& weak_state) {
        const std::shared_ptr<LiveWakeState> state = weak_state.lock();
        if (!state ||
            !(*state).connected.load(std::memory_order_acquire)) {
            return;
        }
        const std::shared_ptr<RasterControl> target = weak_target.lock();
        if (target) {
            if (Window* owner = (*target).window();
                owner != nullptr &&
                (*owner).queue_live_surface_presentation(
                    target, (*target).live_surface_)) {
                // The host consumes the newest immutable generation as a
                // compositor layer. Rearm publication now; no retained paint
                // transaction is outstanding.
                (*state).queued.store(false, std::memory_order_release);
                return;
            }
            (*target).invalidate(gui_forms::invalidation::paint_only);
        }
        // Do not rearm here. The retained paint transaction owns release after
        // it samples the newest complete candidate.
    }

    static void queue_live_surface_paint(
        const std::weak_ptr<RasterControl>& weak_target,
        const std::weak_ptr<LiveWakeState>& weak_state) noexcept {
        const std::shared_ptr<gui_forms::abi::detail::RasterControl::LiveWakeState> state = weak_state.lock();
        if (!state || !(*state).connected.load(std::memory_order_acquire) ||
            (*state).queued.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const std::shared_ptr<gui_forms::abi::detail::RasterControl> target = weak_target.lock();
        if (!target) {
            (*state).queued.store(false, std::memory_order_release);
            return;
        }
        try {
            static_cast<void>((*target).begin_invoke(
                DeferredLiveSurfacePaint{weak_target, weak_state}));
        } catch (...) {
            (*state).queued.store(false, std::memory_order_release);
        }
    }

    void connect_live_surface_wake() {
        disconnect_live_surface_wake();
        if (!live_surface_ || window() == nullptr) return;
        const std::shared_ptr<gui_forms::abi::detail::RasterControl> self = std::static_pointer_cast<RasterControl>(
            shared_from_this());
        if ((*window()).queue_live_surface_presentation(self, live_surface_)) {
            // Registration is persistent. The terminal host display clock now
            // samples newest generations; producer publication must not post
            // one dispatcher callback per frame.
            live_surface_direct_ = true;
            return;
        }
        std::shared_ptr<gui_forms::abi::detail::RasterControl::LiveWakeState> wake_state = std::make_shared<LiveWakeState>();
        live_wake_state_ = wake_state;
        const std::weak_ptr<RasterControl> weak_target =
            self;
        live_wake_ = (*live_surface_).connect_presentation_wake(
            LiveSurfaceWakeCallback{
                weak_target, std::weak_ptr<LiveWakeState>(wake_state)});
    }

    void disconnect_live_surface_wake() noexcept {
        live_surface_direct_ = false;
        if (live_wake_state_) {
            (*live_wake_state_).connected.store(false, std::memory_order_release);
        }
        live_wake_.disconnect();
        live_wake_state_.reset();
    }

    bool input_transparent_{};
    std::vector<std::byte> encoded_;
    ImageResourceEncoding encoding_{ImageResourceEncoding::png};
    std::uint32_t pixel_width_{};
    std::uint32_t pixel_height_{};
    std::uint64_t pixel_row_bytes_{};
    gui_forms::ImageId image_{};
    std::shared_ptr<gui_forms::LiveSurface> live_surface_;
    bool live_surface_direct_{};
    gui_forms::LiveSurfaceWakeConnection live_wake_;
    std::shared_ptr<LiveWakeState> live_wake_state_;
    gui_forms::Event<const RasterPointerSample&> pointer_input_;
    gui_forms::Event<const RasterKeySample&> key_input_;
};

} // namespace gui_forms::abi::detail
