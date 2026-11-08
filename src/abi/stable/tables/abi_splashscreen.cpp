// GF_TABLE_RUNTIME's splashscreen functions (since 1.1): the C spelling of
// Splashscreen (APP_WINDOW_RULES_DESIGN Part S). See gf_abi.h.
//
// The library holds at most one splashscreen, from splashscreen_show to
// splashscreen_close, under a handle that names it; the C++ handover (the
// library's first window closes the window) leaves the handle open, so the
// setters then accept and present nothing, as Splashscreen's do. Like the
// rest of the runtime table these functions return failures without
// recording them. They run on the thread that showed the splashscreen; the
// state is guarded by one mutex so that another thread is refused cleanly.

#include "abi_tables.hpp"

#include "gui_forms/application/splashscreen/splashscreen.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace gui_forms::abi1 {
namespace {

// The 1.1 size of gf_splashscreen_options: a caller passes at least this much.
constexpr std::uint32_t splashscreen_options_size_1_1 = 48U;
static_assert(sizeof(gf_splashscreen_options) == splashscreen_options_size_1_1);

// The library's one splashscreen: the C++ object while its handle is open,
// the handle and the thread that showed it. Handles count from one and are
// never reused, so a closed handle stays stale.
struct SplashscreenState final {
    std::mutex mutex{};
    std::unique_ptr<Splashscreen> shown{};
    gf_splashscreen handle{};
    std::thread::id thread{};
    gf_splashscreen last_handle{};
};

SplashscreenState& splashscreen_state() noexcept {
    static SplashscreenState state;
    return state;
}

gf_status splashscreen_status(const SplashscreenError error) noexcept {
    switch (error) {
    case SplashscreenError::none: return GF_STATUS_OK;
    case SplashscreenError::invalid_argument: return GF_STATUS_INVALID_ARGUMENT;
    case SplashscreenError::already_shown: return GF_STATUS_INVALID_STATE;
    case SplashscreenError::unsupported: return GF_STATUS_UNSUPPORTED_CAPABILITY;
    default: return GF_STATUS_INTERNAL;
    }
}

// The view's text, or none when its data is null with a non-zero size.
std::optional<std::string_view> view_text(const gf_utf8_view view) noexcept {
    if (view.data == nullptr) {
        if (view.size != 0U) return std::nullopt;
        return std::string_view{};
    }
    const std::string_view text(view.data, static_cast<std::size_t>(view.size));
    return text;
}

// The progress a call asks for: none without show_progress; a progress out
// of [0, 1] is passed on for Splashscreen to refuse.
std::optional<double> requested_progress(const std::uint32_t show_progress, const double progress) noexcept {
    if (show_progress == 0U) return std::nullopt;
    return progress;
}

// Checks that `handle` is the open splashscreen's and that this is its
// thread. The caller holds the state's mutex.
gf_status check_handle(const SplashscreenState& state, const gf_splashscreen handle) noexcept {
    if (handle == 0U || handle != state.handle || !state.shown) return GF_STATUS_STALE_HANDLE;
    if (std::this_thread::get_id() != state.thread) return GF_STATUS_WRONG_THREAD;
    return GF_STATUS_OK;
}

} // namespace

gf_status GF_CALL runtime_splashscreen_show(const gf_splashscreen_options* options,
                                            gf_splashscreen* splashscreen) noexcept {
    try {
        if (options == nullptr || splashscreen == nullptr ||
            (*options).struct_size < splashscreen_options_size_1_1 ||
            ((*options).artwork == nullptr && (*options).artwork_size != 0U)) {
            return GF_STATUS_INVALID_ARGUMENT;
        }
        const std::optional<std::string_view> status = view_text((*options).status);
        if (!status.has_value()) return GF_STATUS_INVALID_ARGUMENT;
        SplashscreenOptions made{};
        const std::byte* artwork = reinterpret_cast<const std::byte*>((*options).artwork);
        made.artwork.assign(artwork, artwork + static_cast<std::size_t>((*options).artwork_size));
        made.status.assign(*status);
        made.progress = requested_progress((*options).show_progress, (*options).progress);
        SplashscreenState& state = splashscreen_state();
        const std::lock_guard<std::mutex> lock(state.mutex);
        if (state.shown) return GF_STATUS_INVALID_STATE;
        SplashscreenResult result = Splashscreen::show(std::move(made));
        if (!result) return splashscreen_status(result.error);
        state.last_handle = state.last_handle + 1U;
        state.handle = state.last_handle;
        state.thread = std::this_thread::get_id();
        state.shown = std::move(result.splashscreen);
        *splashscreen = state.handle;
        return GF_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return GF_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return GF_STATUS_INTERNAL;
    }
}

gf_status GF_CALL runtime_splashscreen_set_status(const gf_splashscreen splashscreen,
                                                  const gf_utf8_view status) noexcept {
    try {
        SplashscreenState& state = splashscreen_state();
        const std::lock_guard<std::mutex> lock(state.mutex);
        const gf_status checked = check_handle(state, splashscreen);
        if (checked != GF_STATUS_OK) return checked;
        const std::optional<std::string_view> text = view_text(status);
        if (!text.has_value()) return GF_STATUS_INVALID_ARGUMENT;
        const SplashscreenError error = (*state.shown).set_status(*text);
        return splashscreen_status(error);
    } catch (const std::bad_alloc&) {
        return GF_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return GF_STATUS_INTERNAL;
    }
}

gf_status GF_CALL runtime_splashscreen_set_progress(const gf_splashscreen splashscreen,
                                                    const std::uint32_t show_progress,
                                                    const double progress) noexcept {
    try {
        SplashscreenState& state = splashscreen_state();
        const std::lock_guard<std::mutex> lock(state.mutex);
        const gf_status checked = check_handle(state, splashscreen);
        if (checked != GF_STATUS_OK) return checked;
        const SplashscreenError error = (*state.shown).set_progress(requested_progress(show_progress, progress));
        return splashscreen_status(error);
    } catch (const std::bad_alloc&) {
        return GF_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return GF_STATUS_INTERNAL;
    }
}

gf_status GF_CALL runtime_splashscreen_close(const gf_splashscreen splashscreen) noexcept {
    try {
        std::unique_ptr<Splashscreen> closing{};
        {
            SplashscreenState& state = splashscreen_state();
            const std::lock_guard<std::mutex> lock(state.mutex);
            const gf_status checked = check_handle(state, splashscreen);
            if (checked != GF_STATUS_OK) return checked;
            closing = std::move(state.shown);
            state.handle = 0U;
        }
        // Destroyed outside the lock: the destructor closes the host window.
        closing.reset();
        return GF_STATUS_OK;
    } catch (...) {
        return GF_STATUS_INTERNAL;
    }
}

} // namespace gui_forms::abi1
