#include "demo_parts.hpp"
#include "gui_forms/controls/choice_group.hpp"
#include "gui_forms/controls/drawing_surface.hpp"
#include "gui_forms/controls/list_view.hpp"
#include "gui_forms/theme/owner_draw/owner_draw.hpp"
#include "gui_forms/timer/timer/timer.hpp"
#include "stxlib/math.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The media-and-motion page: four DevExpress media takes the survey rates
// "demo only", composed from stock parts with no new API.
// - Image slider: painted slides on a DrawingSurface with previous and next
//   Buttons; a Timer slides from one to the next.
// - Thumbnail gallery: an owner-drawn ListView in tile view whose
//   selection drives the slider.
// - Transitions: two painted scenes and a ChoiceGroup of effects
//   (crossfade, slide, push, wipe) played by a Timer.
// - Carousel: cards on a DrawingSurface turning round a centre card, with
//   dot Buttons and an autoplay toggle.
// Every motion honours the window's reduced-motion setting: the change then
// happens at once, and autoplay steps without the turn. A Timer runs only
// while something moves, so an idle page wakes nobody.
namespace gui_forms::gallery::pages {
namespace {

// -- slides -----------------------------------------------------------------
// A slide is a painted scene: a two-colour sky over a ground band, a sun
// and a caption. The page has no photographs, so these stand in for them.
struct Slide final {
    std::string_view id{};
    std::string_view caption{};
    std::string_view album{};
    Color sky{};
    Color ground{};
    Color sun{};
};

constexpr std::array<Slide, 8> slides{{
    {"slide.harbour", "Harbour at dawn", "Coast", Color::rgba(250, 196, 140), Color::rgba(40, 90, 130),
     Color::rgba(255, 236, 180)},
    {"slide.dunes", "Dunes", "Coast", Color::rgba(160, 205, 235), Color::rgba(222, 186, 120),
     Color::rgba(255, 250, 220)},
    {"slide.cliffs", "Chalk cliffs", "Coast", Color::rgba(190, 210, 228), Color::rgba(90, 140, 90),
     Color::rgba(250, 250, 240)},
    {"slide.ridge", "Ridge walk", "Hills", Color::rgba(120, 170, 220), Color::rgba(70, 110, 70),
     Color::rgba(255, 244, 200)},
    {"slide.tarn", "Mountain tarn", "Hills", Color::rgba(98, 140, 190), Color::rgba(46, 80, 96),
     Color::rgba(236, 240, 250)},
    {"slide.meadow", "Hay meadow", "Hills", Color::rgba(176, 214, 240), Color::rgba(128, 170, 70),
     Color::rgba(255, 238, 150)},
    {"slide.station", "Night station", "Town", Color::rgba(30, 36, 70), Color::rgba(60, 60, 70),
     Color::rgba(240, 230, 170)},
    {"slide.market", "Market square", "Town", Color::rgba(236, 170, 120), Color::rgba(150, 90, 70),
     Color::rgba(255, 220, 160)},
}};

constexpr std::array<std::string_view, 3> albums{"Coast", "Hills", "Town"};

// An album's group ID: a stable ID segment is lower case ("album.coast").
std::string album_group(std::string_view album) {
    std::string group = "album.";
    for (const char letter : album) {
        group.push_back(letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter);
    }
    return group;
}

[[nodiscard]] Color mix(Color from, Color to, double amount) {
    const double kept = 1.0 - amount;
    const Color result = Color::rgba(
        static_cast<std::uint8_t>(std::lround(from.red * kept + to.red * amount)),
        static_cast<std::uint8_t>(std::lround(from.green * kept + to.green * amount)),
        static_cast<std::uint8_t>(std::lround(from.blue * kept + to.blue * amount)),
        static_cast<std::uint8_t>(std::lround(from.alpha * kept + to.alpha * amount)));
    return result;
}

// Paints `slide` into `area`, its colours faded towards `veil` by `fade`
// (zero for none): the transitions crossfade with it.
void paint_slide(Painter& painter, const Slide& slide, Rect area, double fade, Color veil) {
    const Color sky = mix(slide.sky, veil, fade);
    const Color ground = mix(slide.ground, veil, fade);
    const Color sun = mix(slide.sun, veil, fade);
    painter.fill_rect(area, sky);
    const double horizon = area.y + area.height * 0.62;
    painter.fill_rect({area.x, horizon, area.width, area.y + area.height - horizon}, ground);
    const double radius = area.height * 0.12;
    const Point centre{area.x + area.width * 0.72, area.y + area.height * 0.32};
    std::array<Point, 24> disc{};
    for (std::size_t corner = 0; corner < disc.size(); ++corner) {
        const double angle = 6.283185307179586 * static_cast<double>(corner) / static_cast<double>(disc.size());
        disc[corner] = Point{centre.x + radius * stxlib::math::cos(angle), centre.y + radius * stxlib::math::sin(angle)};
    }
    painter.fill_polygon(disc, sun, PolygonFillRule::nonzero);
    const std::array<Point, 3> hill{Point{area.x, horizon}, Point{area.x + area.width * 0.35, horizon - area.height * 0.2},
                                    Point{area.x + area.width * 0.7, horizon}};
    painter.fill_polygon(hill, mix(ground, Color::rgba(0, 0, 0), 0.18), PolygonFillRule::nonzero);
    const double caption_size = std::max(9.0, area.height * 0.07);
    painter.draw_text_utf8({area.x + 12.0, area.y + area.height - 12.0}, slide.caption,
                           FontSpec{FontRole::content, caption_size, 600, false}, mix(Color::rgba(255, 255, 255), veil, fade));
}

// -- image slider -------------------------------------------------------------
// What the slider draws: the slide shown, the one coming and how far the
// slide has gone (0 to 1), in `direction` (+1 from the right, -1 from the
// left).
struct SliderState final {
    std::size_t shown{};
    std::size_t coming{};
    double progress{};
    int direction{1};
};

class SliderPaint final {
public:
    explicit SliderPaint(std::shared_ptr<const SliderState> state) : state_(std::move(state)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        const SliderState& state = *state_;
        painter.save();
        painter.clip_rect(bounds);
        const double offset = bounds.width * state.progress * static_cast<double>(state.direction);
        const Rect out{bounds.x - offset, bounds.y, bounds.width, bounds.height};
        paint_slide(painter, slides[state.shown], out, 0.0, Color::rgba(0, 0, 0));
        if (state.progress > 0.0) {
            const Rect in{out.x + bounds.width * static_cast<double>(state.direction), bounds.y, bounds.width,
                          bounds.height};
            paint_slide(painter, slides[state.coming], in, 0.0, Color::rgba(0, 0, 0));
        }
        painter.restore();
    }

private:
    std::shared_ptr<const SliderState> state_;
};

// -- transitions --------------------------------------------------------------
enum class Effect : std::uint8_t { crossfade, slide, push, wipe };

struct TransitionState final {
    Effect effect{Effect::crossfade};
    // Which scene is the "from" one: the scenes swap after each play.
    bool reversed{};
    double progress{};
};

class TransitionPaint final {
public:
    explicit TransitionPaint(std::shared_ptr<const TransitionState> state) : state_(std::move(state)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        const TransitionState& state = *state_;
        const Slide& from = state.reversed ? slides[6] : slides[1];
        const Slide& to = state.reversed ? slides[1] : slides[6];
        const double amount = state.progress;
        painter.save();
        painter.clip_rect(bounds);
        if (state.effect == Effect::crossfade) {
            // Each scene's colours go towards the other's average; at the
            // middle both are the same wash, then the new scene clears.
            if (amount < 0.5) {
                paint_slide(painter, from, bounds, amount * 2.0, mix(to.sky, to.ground, 0.5));
            } else {
                paint_slide(painter, to, bounds, (1.0 - amount) * 2.0, mix(from.sky, from.ground, 0.5));
            }
        } else if (state.effect == Effect::slide) {
            paint_slide(painter, from, bounds, 0.0, Color::rgba(0, 0, 0));
            paint_slide(painter, to, {bounds.x + bounds.width * (1.0 - amount), bounds.y, bounds.width, bounds.height},
                        0.0, Color::rgba(0, 0, 0));
        } else if (state.effect == Effect::push) {
            const double shift = bounds.width * amount;
            paint_slide(painter, from, {bounds.x - shift, bounds.y, bounds.width, bounds.height}, 0.0,
                        Color::rgba(0, 0, 0));
            paint_slide(painter, to, {bounds.x + bounds.width - shift, bounds.y, bounds.width, bounds.height}, 0.0,
                        Color::rgba(0, 0, 0));
        } else {
            paint_slide(painter, from, bounds, 0.0, Color::rgba(0, 0, 0));
            painter.clip_rect({bounds.x, bounds.y, bounds.width * amount, bounds.height});
            paint_slide(painter, to, bounds, 0.0, Color::rgba(0, 0, 0));
        }
        painter.restore();
    }

private:
    std::shared_ptr<const TransitionState> state_;
};

// -- carousel -----------------------------------------------------------------
constexpr std::size_t carousel_count = 5U;

struct CarouselState final {
    // The card at the front, as a position that moves continuously while
    // the carousel turns (2.4 is between cards 2 and 3).
    double position{};
};

class CarouselPaint final {
public:
    explicit CarouselPaint(std::shared_ptr<const CarouselState> state) : state_(std::move(state)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        painter.save();
        painter.clip_rect(bounds);
        // Back cards first: order by distance from the front, farthest
        // first, so the front card paints last.
        std::array<std::size_t, carousel_count> order{};
        std::array<double, carousel_count> offsets{};
        for (std::size_t card = 0; card < carousel_count; ++card) {
            double offset = static_cast<double>(card) - (*state_).position;
            const double half = static_cast<double>(carousel_count) * 0.5;
            while (offset > half) offset -= static_cast<double>(carousel_count);
            while (offset < -half) offset += static_cast<double>(carousel_count);
            offsets[card] = offset;
            order[card] = card;
        }
        const FartherFirst farther{&offsets};
        std::sort(order.begin(), order.end(), farther);
        for (const std::size_t card : order) {
            const double offset = offsets[card];
            const double distance = std::fabs(offset);
            const double scale = std::max(0.45, 1.0 - 0.22 * distance);
            const double width = bounds.height * 0.9 * scale;
            const double height = bounds.height * 0.8 * scale;
            const double centre_x = bounds.x + bounds.width * 0.5 + offset * bounds.width * 0.22;
            const Rect card_bounds{centre_x - width * 0.5, bounds.y + (bounds.height - height) * 0.5, width, height};
            const Slide& slide = slides[(card * 3U) % slides.size()];
            painter.save();
            painter.clip_rounded_rect(card_bounds, 8.0);
            paint_slide(painter, slide, card_bounds, std::min(0.6, distance * 0.25), Color::rgba(236, 240, 245));
            painter.restore();
            painter.stroke_rounded_rect(card_bounds, 8.0, Color::rgba(255, 255, 255), 2.0);
        }
        painter.restore();
    }

private:
    struct FartherFirst final {
        const std::array<double, carousel_count>* offsets{};

        bool operator()(std::size_t left, std::size_t right) const {
            return std::fabs((*offsets)[right]) < std::fabs((*offsets)[left]);
        }
    };

    std::shared_ptr<const CarouselState> state_;
};

// The motion step: frames of a move, and a smooth start and stop.
constexpr std::chrono::milliseconds frame_interval{16};
constexpr int move_frames = 18;

[[nodiscard]] double ease(double amount) {
    const double clamped = std::clamp(amount, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

class MediaAndMotionPage final : public gui_forms::Component {
public:
    explicit MediaAndMotionPage(const std::shared_ptr<Surface>& body) : body_(body) {}

    void build() {
        Surface& body = *body_;
        add_page_heading(body, "media", "MEDIA AND MOTION",
                         "An image slider, a thumbnail gallery that drives it, scene transitions and a carousel, "
                         "painted on DrawingSurface and moved by Timers that stop when nothing moves.");
        build_slider(body);
        build_thumbnails(body);
        build_transitions(body);
        build_carousel(body);
        status_ = label("gallery.media.status", "Reduced motion makes every change immediate.", 11.0, 400, "muted");
        body.add_at(status_, {24.0, 640.0, 940.0, 22.0});
    }

    void connect() {
        on((*slider_previous_).clicked(), *this, &MediaAndMotionPage::on_slider_previous);
        on((*slider_next_).clicked(), *this, &MediaAndMotionPage::on_slider_next);
        on((*thumbnails_).draw_item_content(), *this, &MediaAndMotionPage::on_draw_thumbnail);
        on((*thumbnails_).selection_changed(), *this, &MediaAndMotionPage::on_thumbnail_selected);
        on((*effects_).changed(), *this, &MediaAndMotionPage::on_effect_changed);
        on((*play_transition_).clicked(), *this, &MediaAndMotionPage::on_play_transition);
        for (std::size_t dot = 0; dot < carousel_dots_.size(); ++dot) {
            on((*carousel_dots_[dot]).clicked(), *this, &MediaAndMotionPage::on_carousel_dot, dot);
        }
        on((*carousel_autoplay_).clicked(), *this, &MediaAndMotionPage::on_carousel_autoplay);
    }

private:
    // -- slider -------------------------------------------------------------
    void build_slider(Surface& body) {
        const std::shared_ptr<LayoutGroup> slider = group("gallery.media.slider", "Image slider", {470.0, 270.0});
        body.add_at(slider, {24.0, 92.0, 470.0, 270.0});
        slider_state_ = gui_forms::detail::make_library_shared<SliderState>();
        slider_view_ = make_control<DrawingSurface>(StableId("gallery.media.slider.view"));
        (*slider_view_).set_paint_callback(SliderPaint(slider_state_));
        (*slider).add_at(slider_view_, {12.0, 30.0, 446.0, 190.0});
        slider_previous_ = make_control<Button>(StableId("gallery.media.slider.previous"), "<");
        (*slider_previous_).accessible_name = "Previous slide";
        (*slider).add_at(slider_previous_, {12.0, 228.0, 40.0, 28.0});
        slider_caption_ = label("gallery.media.slider.caption", "", 11.0, 600, "ink");
        static_cast<void>((*slider_caption_).add_class("centered"));
        (*slider).add_at(slider_caption_, {60.0, 232.0, 350.0, 22.0});
        slider_next_ = make_control<Button>(StableId("gallery.media.slider.next"), ">");
        (*slider_next_).accessible_name = "Next slide";
        (*slider).add_at(slider_next_, {418.0, 228.0, 40.0, 28.0});
        show_slide();
    }

    void on_slider_previous() { go_to_slide((slider_target() + slides.size() - 1U) % slides.size(), -1); }
    void on_slider_next() { go_to_slide((slider_target() + 1U) % slides.size(), 1); }

    // The slide the slider shows or is moving to.
    [[nodiscard]] std::size_t slider_target() const {
        return slider_frame_ > 0 ? (*slider_state_).coming : (*slider_state_).shown;
    }

    void go_to_slide(std::size_t slide, int direction) {
        SliderState& state = *slider_state_;
        if (slider_frame_ > 0) {
            // A move in progress lands first.
            state.shown = state.coming;
            state.progress = 0.0;
            slider_frame_ = 0;
        }
        if (slide == state.shown) {
            show_slide();
            return;
        }
        state.coming = slide;
        state.direction = direction;
        if (reduced_motion_for(*slider_view_) || !ensure_timer()) {
            state.shown = slide;
            show_slide();
            return;
        }
        slider_frame_ = 1;
        show_slide();
        (*timer_).start();
    }

    void show_slide() {
        const Slide& slide = slides[slider_target()];
        (*slider_caption_).text = std::string(slide.caption) + "  ·  " + std::to_string(slider_target() + 1U) + " of " +
                                  std::to_string(slides.size());
        (*slider_view_).accessible_name = "Slide: " + std::string(slide.caption);
        (*slider_view_).invalidate(Dirty::paint);
        if (!thumbnails_) return;
        const std::vector<std::size_t> selected = (*thumbnails_).selected_indices();
        const bool already = selected.size() == 1U && selected.front() == slider_target();
        if (!already) (*thumbnails_).set_selected(slider_target(), true);
    }

    // -- thumbnails ---------------------------------------------------------
    void build_thumbnails(Surface& body) {
        const std::shared_ptr<LayoutGroup> gallery =
            group("gallery.media.thumbnails", "Thumbnail gallery", {470.0, 250.0});
        body.add_at(gallery, {24.0, 376.0, 470.0, 250.0});
        thumbnails_ = make_control<ListView>(StableId("gallery.media.thumbnails.list"));
        (*thumbnails_).accessible_name = "Thumbnails";
        (*thumbnails_).view = ListViewView::tile;
        (*thumbnails_).tile_size = Size{140.0, 44.0};
        (*thumbnails_).multi_select = false;
        (*thumbnails_).draw_mode = ItemDrawMode::owner_draw;
        std::vector<ListViewGroup> groups{};
        for (const std::string_view album : albums) {
            groups.push_back(ListViewGroup{album_group(album), std::string(album)});
        }
        (*thumbnails_).set_groups(std::move(groups));
        std::vector<ListViewItem> items{};
        for (const Slide& slide : slides) {
            ListViewItem item{};
            item.stable_id = std::string(slide.id);
            item.text = std::string(slide.caption);
            item.group = album_group(slide.album);
            items.push_back(std::move(item));
        }
        (*thumbnails_).set_items(std::move(items));
        (*gallery).add_at(thumbnails_, {12.0, 30.0, 446.0, 208.0});
    }

    // Each tile: a small painting of its slide, then its caption.
    void on_draw_thumbnail(ItemContentPaint& request) {
        const Slide* found = nullptr;
        for (const Slide& slide : slides) {
            if (slide.id == request.stable_id) found = &slide;
        }
        if (found == nullptr) {
            request.draw_default = true;
            return;
        }
        const Rect content = request.content;
        const double side = std::max(0.0, std::min(content.height, 52.0));
        const Rect swatch{content.x, content.y + (content.height - side) * 0.5, side * 1.3, side};
        request.painter.save();
        request.painter.clip_rounded_rect(swatch, 4.0);
        paint_slide(request.painter, *found, swatch, 0.0, Color::rgba(0, 0, 0));
        request.painter.restore();
        request.painter.draw_text_utf8({swatch.x + swatch.width + 8.0, content.y + content.height * 0.5 + 4.0},
                                       (*found).caption, request.font, request.text);
        request.draw_default = false;
    }

    void on_thumbnail_selected(const ListViewSelectionChange& change) {
        static_cast<void>(change);
        const std::vector<std::size_t> selected = (*thumbnails_).selected_indices();
        if (selected.size() != 1U || selected.front() >= slides.size()) return;
        const std::size_t slide = selected.front();
        if (slide == slider_target()) return;
        go_to_slide(slide, slide > slider_target() ? 1 : -1);
        report("Thumbnail " + std::string(slides[slide].caption) + " drives the slider");
    }

    // -- transitions --------------------------------------------------------
    void build_transitions(Surface& body) {
        const std::shared_ptr<LayoutGroup> transitions =
            group("gallery.media.transitions", "Transitions", {466.0, 270.0});
        body.add_at(transitions, {510.0, 92.0, 466.0, 270.0});
        transition_state_ = gui_forms::detail::make_library_shared<TransitionState>();
        transition_view_ = make_control<DrawingSurface>(StableId("gallery.media.transitions.view"));
        (*transition_view_).accessible_name = "Transition preview";
        (*transition_view_).set_paint_callback(TransitionPaint(transition_state_));
        (*transitions).add_at(transition_view_, {12.0, 30.0, 442.0, 190.0});
        effects_ = make_control<ChoiceGroup>(StableId("gallery.media.transitions.effect"));
        (*effects_).accessible_name = "Effect";
        (*effects_).set_items({ChoiceItem{0, "Crossfade"}, ChoiceItem{1, "Slide"}, ChoiceItem{2, "Push"},
                               ChoiceItem{3, "Wipe"}});
        (*effects_).selected_index = 0;
        (*transitions).add_at(effects_, {12.0, 228.0, 330.0, 28.0});
        play_transition_ = make_control<Button>(StableId("gallery.media.transitions.play"), "Play");
        (*transitions).add_at(play_transition_, {354.0, 228.0, 100.0, 28.0});
    }

    void on_effect_changed(int index) {
        const int clamped = std::clamp(index, 0, 3);
        (*transition_state_).effect = static_cast<Effect>(clamped);
        report("Effect: " + std::string(std::array<std::string_view, 4>{"crossfade", "slide", "push", "wipe"}
                                            [static_cast<std::size_t>(clamped)]));
    }

    void on_play_transition() {
        TransitionState& state = *transition_state_;
        if (transition_frame_ > 0) return;
        if (reduced_motion_for(*transition_view_) || !ensure_timer()) {
            state.reversed = !state.reversed;
            state.progress = 0.0;
            (*transition_view_).invalidate(Dirty::paint);
            report("Transition shown at once (reduced motion)");
            return;
        }
        transition_frame_ = 1;
        (*timer_).start();
    }

    // -- carousel -----------------------------------------------------------
    void build_carousel(Surface& body) {
        const std::shared_ptr<LayoutGroup> carousel = group("gallery.media.carousel", "Carousel", {466.0, 250.0});
        body.add_at(carousel, {510.0, 376.0, 466.0, 250.0});
        carousel_state_ = gui_forms::detail::make_library_shared<CarouselState>();
        carousel_view_ = make_control<DrawingSurface>(StableId("gallery.media.carousel.view"));
        (*carousel_view_).set_paint_callback(CarouselPaint(carousel_state_));
        (*carousel).add_at(carousel_view_, {12.0, 30.0, 442.0, 170.0});
        for (std::size_t dot = 0; dot < carousel_dots_.size(); ++dot) {
            const std::string id = "gallery.media.carousel.dot." + std::to_string(dot);
            std::shared_ptr<Button> button = make_control<Button>(StableId(id), std::to_string(dot + 1U));
            (*button).accessible_name = "Card " + std::to_string(dot + 1U);
            (*carousel).add_at(button, {12.0 + 34.0 * static_cast<double>(dot), 208.0, 30.0, 28.0});
            carousel_dots_[dot] = button;
        }
        carousel_autoplay_ = make_control<Button>(StableId("gallery.media.carousel.autoplay"), "Autoplay: off");
        (*carousel).add_at(carousel_autoplay_, {324.0, 208.0, 130.0, 28.0});
        show_carousel();
    }

    void on_carousel_dot(std::size_t dot) {
        turn_carousel_to(dot);
        report("Card " + std::to_string(dot + 1U) + " to the front");
    }

    void on_carousel_autoplay() {
        autoplay_ = !autoplay_;
        autoplay_ticks_ = 0;
        (*carousel_autoplay_).text = autoplay_ ? "Autoplay: on" : "Autoplay: off";
        if (autoplay_ && ensure_timer()) (*timer_).start();
        report(autoplay_ ? "Autoplay turns the carousel every three seconds" : "Autoplay stopped");
    }

    void turn_carousel_to(std::size_t card) {
        carousel_from_ = (*carousel_state_).position;
        double target = static_cast<double>(card);
        // The short way round.
        const double count = static_cast<double>(carousel_count);
        while (target - carousel_from_ > count * 0.5) target -= count;
        while (carousel_from_ - target > count * 0.5) target += count;
        carousel_to_ = target;
        carousel_front_ = card;
        if (reduced_motion_for(*carousel_view_) || !ensure_timer()) {
            (*carousel_state_).position = static_cast<double>(card);
            carousel_frame_ = 0;
            show_carousel();
            return;
        }
        carousel_frame_ = 1;
        (*timer_).start();
    }

    void show_carousel() {
        (*carousel_view_).accessible_name = "Carousel, card " + std::to_string(carousel_front_ + 1U) + " of " +
                                            std::to_string(carousel_count);
        (*carousel_view_).invalidate(Dirty::paint);
    }

    // -- the one Timer ------------------------------------------------------
    bool ensure_timer() {
        if (timer_) return true;
        Window* const window = (*slider_view_).attached_window();
        if (window == nullptr) return false;
        timer_ = std::make_unique<Timer>(*window, frame_interval);
        on((*timer_).tick(), *this, &MediaAndMotionPage::on_tick);
        return true;
    }

    // Advances every move in progress; stops the Timer when none is left
    // and autoplay is off.
    void on_tick() {
        if (slider_frame_ > 0) {
            SliderState& state = *slider_state_;
            state.progress = ease(static_cast<double>(slider_frame_) / move_frames);
            if (slider_frame_ >= move_frames) {
                state.shown = state.coming;
                state.progress = 0.0;
                slider_frame_ = 0;
            } else {
                ++slider_frame_;
            }
            (*slider_view_).invalidate(Dirty::paint);
        }
        if (transition_frame_ > 0) {
            TransitionState& state = *transition_state_;
            state.progress = ease(static_cast<double>(transition_frame_) / (move_frames * 2));
            if (transition_frame_ >= move_frames * 2) {
                state.reversed = !state.reversed;
                state.progress = 0.0;
                transition_frame_ = 0;
            } else {
                ++transition_frame_;
            }
            (*transition_view_).invalidate(Dirty::paint);
        }
        if (carousel_frame_ > 0) {
            const double amount = ease(static_cast<double>(carousel_frame_) / move_frames);
            double position = carousel_from_ + (carousel_to_ - carousel_from_) * amount;
            const double count = static_cast<double>(carousel_count);
            while (position < 0.0) position += count;
            while (position >= count) position -= count;
            (*carousel_state_).position = position;
            if (carousel_frame_ >= move_frames) {
                (*carousel_state_).position = static_cast<double>(carousel_front_);
                carousel_frame_ = 0;
            } else {
                ++carousel_frame_;
            }
            show_carousel();
        }
        if (autoplay_) {
            // Three seconds of frames, then the next card.
            ++autoplay_ticks_;
            if (autoplay_ticks_ * frame_interval.count() >= 3000) {
                autoplay_ticks_ = 0;
                turn_carousel_to((carousel_front_ + 1U) % carousel_count);
            }
        }
        const bool moving = slider_frame_ > 0 || transition_frame_ > 0 || carousel_frame_ > 0;
        if (!moving && !autoplay_) (*timer_).stop();
    }

    void report(std::string text) { (*status_).text = std::move(text); }

    std::shared_ptr<Surface> body_;
    std::shared_ptr<Label> status_;
    std::unique_ptr<Timer> timer_;

    std::shared_ptr<SliderState> slider_state_;
    std::shared_ptr<DrawingSurface> slider_view_;
    std::shared_ptr<Button> slider_previous_;
    std::shared_ptr<Button> slider_next_;
    std::shared_ptr<Label> slider_caption_;
    int slider_frame_{};

    std::shared_ptr<ListView> thumbnails_;

    std::shared_ptr<TransitionState> transition_state_;
    std::shared_ptr<DrawingSurface> transition_view_;
    std::shared_ptr<ChoiceGroup> effects_;
    std::shared_ptr<Button> play_transition_;
    int transition_frame_{};

    std::shared_ptr<CarouselState> carousel_state_;
    std::shared_ptr<DrawingSurface> carousel_view_;
    std::array<std::shared_ptr<Button>, carousel_count> carousel_dots_{};
    std::shared_ptr<Button> carousel_autoplay_;
    std::size_t carousel_front_{};
    double carousel_from_{};
    double carousel_to_{};
    int carousel_frame_{};
    bool autoplay_{};
    std::int64_t autoplay_ticks_{};
};

} // namespace

void add_media_and_motion(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    const std::shared_ptr<MediaAndMotionPage> owner = gui_forms::detail::make_library_shared<MediaAndMotionPage>(page);
    (*owner).build();
    (*context).components.add(owner);
    (*owner).connect();
}

} // namespace gui_forms::gallery::pages
