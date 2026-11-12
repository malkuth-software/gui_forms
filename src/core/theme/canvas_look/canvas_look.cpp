#include "gui_forms/theme/canvas_look/canvas_look.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include "../dark_variant/dark_variant.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gui_forms {

namespace {

// The canvas anatomy (the part table in docs/user/theme-language.md, "node
// canvas"), constexpr with static storage. Names are the registered (snake
// case) part names; sheets write them with dashes.
constexpr std::array<AnatomyState, 3> canvas_node_part_states{AnatomyState::hot, AnatomyState::selected,
                                                              AnatomyState::disabled};
constexpr std::array<AnatomyState, 3> canvas_port_part_states{AnatomyState::hot, AnatomyState::drop_accepted,
                                                              AnatomyState::drop_rejected};
constexpr std::array<AnatomyState, 3> canvas_link_part_states{AnatomyState::hot, AnatomyState::selected,
                                                              AnatomyState::invalid};
constexpr std::array<AnatomyState, 2> canvas_lamp_part_states{AnatomyState::checked, AnatomyState::invalid};
constexpr std::array<PartDeclaration, 11> canvas_parts{
    PartDeclaration{.name = "region", .role = PartRole::region, .composed_from = "", .states = {}},
    PartDeclaration{.name = "grid_minor", .role = PartRole::grid_minor, .composed_from = "", .states = {}},
    PartDeclaration{.name = "grid_major", .role = PartRole::grid_major, .composed_from = "", .states = {}},
    PartDeclaration{.name = "node", .role = PartRole::node, .composed_from = "", .states = canvas_node_part_states},
    PartDeclaration{
        .name = "node_header", .role = PartRole::header, .composed_from = "", .states = canvas_node_part_states},
    PartDeclaration{.name = "node_selection", .role = PartRole::selection, .composed_from = "", .states = {}},
    PartDeclaration{.name = "port", .role = PartRole::port, .composed_from = "", .states = canvas_port_part_states},
    PartDeclaration{.name = "link", .role = PartRole::link, .composed_from = "", .states = canvas_link_part_states},
    PartDeclaration{
        .name = "dashed_link", .role = PartRole::link, .composed_from = "", .states = canvas_link_part_states},
    PartDeclaration{.name = "lamp", .role = PartRole::lamp, .composed_from = "", .states = canvas_lamp_part_states},
    PartDeclaration{.name = "marquee", .role = PartRole::marquee, .composed_from = "", .states = {}}};
// The control's states: every state a part takes, with focus and
// availability.
constexpr std::array<AnatomyState, 11> canvas_control_states{
    AnatomyState::normal,        AnatomyState::hot,      AnatomyState::focused,
    AnatomyState::focus_visible, AnatomyState::selected, AnatomyState::checked,
    AnatomyState::enabled,       AnatomyState::disabled, AnatomyState::invalid,
    AnatomyState::drop_accepted, AnatomyState::drop_rejected};
constexpr ControlAnatomy canvas_anatomy{.control_id = node_canvas_control_id,
                                        .parts = canvas_parts,
                                        .states = canvas_control_states,
                                        .custom_states = {},
                                        .item_systems = {}};

// Signal colours the basic style has no slot for: an error red and a
// success green (the stock lamps and invalid links).
constexpr Color stock_signal_red = Color::rgba(180, 65, 60);
constexpr Color stock_signal_red_deep = Color::rgba(155, 28, 28);
constexpr Color stock_signal_red_pale = Color::rgba(253, 228, 225);
constexpr Color stock_success = Color::rgba(46, 125, 79);
constexpr Color stock_success_light = Color::rgba(63, 174, 90);
constexpr Color stock_lamp_error = Color::rgba(217, 70, 62);
constexpr Color clear_colour = Color::rgba(0, 0, 0, 0);

[[nodiscard]] constexpr Color with_alpha(Color colour, std::uint8_t alpha) noexcept {
    return Color::rgba(colour.red, colour.green, colour.blue, alpha);
}

// How the resolution colours what it reads: whether rule colours and stock
// colours are mapped by the dark colour rule, and whether this is forced
// high contrast (the floor's plate, edge and focus colours then stand).
struct CanvasColouring final {
    bool darken_rules{};
    bool darken_stock{};
    bool high_contrast{};
};

// `light` through the dark colour rule for `kind`, keeping its alpha (a
// grid line's or a halo's translucency is the rule's, not the mapping's).
[[nodiscard]] Color dark_keeping_alpha(Color light, detail::DarkColourClass kind) noexcept {
    const Color dark = detail::dark_colour(light, kind);
    return with_alpha(dark, light.alpha);
}

// The colour a part draws: the rule's (or the forced high-contrast
// floor's) when set, else `stock`; either mapped to dark as `colouring`
// says.
[[nodiscard]] Color canvas_colour(const std::optional<Color>& set, Color stock, detail::DarkColourClass kind,
                                  const CanvasColouring& colouring) noexcept {
    if (set.has_value()) {
        if (colouring.darken_rules) return dark_keeping_alpha(*set, kind);
        return *set;
    }
    if (colouring.darken_stock) return dark_keeping_alpha(stock, kind);
    return stock;
}

[[nodiscard]] double canvas_length(const std::optional<double>& set, double stock) noexcept {
    if (set.has_value() && std::isfinite(*set) && *set >= 0.0) return *set;
    return stock;
}

// What one part in one state resolves to: the canvas fields its rules set
// and its text colour (a header's title ink). Copied out of the resolved
// recipe, which borrows the theme.
struct CanvasPartPaint final {
    ControlCanvasPaint canvas{};
    Color text{};
};

// One resolution request: the part's registered name, its recipe family,
// its context and its own states.
struct CanvasPartQuery final {
    std::string_view part;
    ControlVisualRole role{ControlVisualRole::panel};
    ControlVisualContext context{};
    AnatomyStateSet states{};
};

[[nodiscard]] CanvasPartPaint resolve_canvas_part(const Theme& theme, const Control& canvas,
                                                  const CanvasPartQuery& query) {
    const ResolvedControlRecipe resolved =
        theme.resolve(canvas, query.role, query.context, query.states, query.part, node_canvas_control_id);
    const ControlVisualRecipe& recipe = resolved.recipe();
    CanvasPartPaint result{};
    result.canvas = recipe.canvas;
    result.text = recipe.text;
    return result;
}

// The context and states of one resolution: `base` (the canvas's scheme
// and contrast) at rest, then the state's surface, selection and part
// state. Non-disabled parts are also `enabled`.
struct CanvasStateRequest final {
    ControlSurfaceState surface{ControlSurfaceState::normal};
    bool selected{};
    std::optional<AnatomyState> part_state{};
};

[[nodiscard]] CanvasPartQuery canvas_query(std::string_view part, ControlVisualRole role, ControlVisualContext base,
                                           const CanvasStateRequest& request) noexcept {
    CanvasPartQuery query{};
    query.part = part;
    query.role = role;
    query.context = base;
    query.context.surface = request.surface;
    query.context.selected = request.selected;
    query.context.focused = false;
    query.context.defaulted = false;
    const bool disabled = request.surface == ControlSurfaceState::disabled;
    if (!disabled) query.states.insert(AnatomyState::enabled);
    if (!request.part_state.has_value()) query.states.insert(AnatomyState::normal);
    if (request.part_state.has_value()) query.states.insert(*request.part_state);
    return query;
}

[[nodiscard]] CanvasStateRequest node_state_request(std::size_t index) noexcept {
    CanvasStateRequest request{};
    const CanvasNodeState state = static_cast<CanvasNodeState>(index);
    switch (state) {
    case CanvasNodeState::hot:
        request.surface = ControlSurfaceState::hot;
        request.part_state = AnatomyState::hot;
        break;
    case CanvasNodeState::selected:
        request.selected = true;
        request.part_state = AnatomyState::selected;
        break;
    case CanvasNodeState::disabled:
        request.surface = ControlSurfaceState::disabled;
        request.part_state = AnatomyState::disabled;
        break;
    default:
        break;
    }
    return request;
}

[[nodiscard]] CanvasStateRequest port_state_request(std::size_t index) noexcept {
    CanvasStateRequest request{};
    const CanvasPortState state = static_cast<CanvasPortState>(index);
    switch (state) {
    case CanvasPortState::hot:
        request.surface = ControlSurfaceState::hot;
        request.part_state = AnatomyState::hot;
        break;
    case CanvasPortState::drop_accepted:
        request.selected = true;
        request.part_state = AnatomyState::drop_accepted;
        break;
    case CanvasPortState::drop_rejected:
        request.surface = ControlSurfaceState::invalid;
        request.part_state = AnatomyState::drop_rejected;
        break;
    default:
        break;
    }
    return request;
}

[[nodiscard]] CanvasStateRequest link_state_request(std::size_t index) noexcept {
    CanvasStateRequest request{};
    const CanvasLinkState state = static_cast<CanvasLinkState>(index);
    switch (state) {
    case CanvasLinkState::hot:
        request.surface = ControlSurfaceState::hot;
        request.part_state = AnatomyState::hot;
        break;
    case CanvasLinkState::selected:
        request.selected = true;
        request.part_state = AnatomyState::selected;
        break;
    case CanvasLinkState::invalid:
        request.surface = ControlSurfaceState::invalid;
        request.part_state = AnatomyState::invalid;
        break;
    default:
        break;
    }
    return request;
}

[[nodiscard]] CanvasStateRequest lamp_state_request(std::size_t index) noexcept {
    CanvasStateRequest request{};
    const CanvasLampState state = static_cast<CanvasLampState>(index);
    switch (state) {
    case CanvasLampState::on:
        request.selected = true;
        request.part_state = AnatomyState::checked;
        break;
    case CanvasLampState::error:
        request.surface = ControlSurfaceState::invalid;
        request.part_state = AnatomyState::invalid;
        break;
    default:
        break;
    }
    return request;
}

// The stock values of one area part in one state (light colours; the
// dark rule maps them), and the dark classes of its fill and line.
struct CanvasAreaStock final {
    Color fill{};
    Color stroke{};
    double stroke_width{};
    double corner_radius{};
    detail::DarkColourClass fill_class{detail::DarkColourClass::stock_surface};
    detail::DarkColourClass stroke_class{detail::DarkColourClass::line};
};

[[nodiscard]] CanvasArea canvas_area(const ControlCanvasPaint& set, const CanvasAreaStock& stock,
                                     const CanvasColouring& colouring) noexcept {
    CanvasArea area{};
    area.fill = canvas_colour(set.fill, stock.fill, stock.fill_class, colouring);
    area.stroke = canvas_colour(set.stroke, stock.stroke, stock.stroke_class, colouring);
    area.stroke_width = canvas_length(set.stroke_width, stock.stroke_width);
    area.corner_radius = canvas_length(set.corner_radius, stock.corner_radius);
    return area;
}

// The stock values of one link state, with its line's dark class.
struct CanvasLineStock final {
    Color colour{};
    double width{2.0};
    double dash{};
    double gap{};
    Color halo{};
    double halo_width{};
    detail::DarkColourClass colour_class{detail::DarkColourClass::line};
};

[[nodiscard]] CanvasLine canvas_line(const ControlCanvasPaint& set, const CanvasLineStock& stock,
                                     const CanvasColouring& colouring) noexcept {
    CanvasLine line{};
    line.colour = canvas_colour(set.stroke, stock.colour, stock.colour_class, colouring);
    line.width = canvas_length(set.stroke_width, stock.width);
    line.dash = canvas_length(set.dash, stock.dash);
    line.gap = canvas_length(set.gap, stock.gap);
    line.halo = canvas_colour(set.halo, stock.halo, detail::DarkColourClass::cue, colouring);
    line.halo_width = canvas_length(set.halo_width, stock.halo_width);
    return line;
}

[[nodiscard]] CanvasAreaStock node_stock(const BasicControlStyle& basic, CanvasNodeState state) noexcept {
    CanvasAreaStock stock{};
    stock.fill = basic.paper;
    stock.stroke = basic.border;
    stock.stroke_width = 1.0;
    stock.corner_radius = 6.0;
    if (state == CanvasNodeState::hot) stock.stroke = basic.dark_border;
    if (state == CanvasNodeState::selected) stock.stroke = basic.accent;
    if (state == CanvasNodeState::selected) stock.stroke_class = detail::DarkColourClass::cue;
    if (state == CanvasNodeState::disabled) stock.fill = basic.face_light;
    return stock;
}

[[nodiscard]] CanvasAreaStock header_stock(const BasicControlStyle& basic, CanvasNodeState state) noexcept {
    CanvasAreaStock stock{};
    stock.fill = basic.face;
    stock.stroke = basic.border;
    stock.stroke_width = 1.0;
    stock.corner_radius = 6.0;
    stock.fill_class = detail::DarkColourClass::chrome_surface;
    if (state == CanvasNodeState::hot) stock.fill = basic.face_light;
    if (state == CanvasNodeState::selected) stock.fill = basic.accent_light;
    if (state == CanvasNodeState::disabled) stock.fill = basic.face_light;
    return stock;
}

[[nodiscard]] CanvasAreaStock port_stock(const BasicControlStyle& basic, CanvasPortState state) noexcept {
    CanvasAreaStock stock{};
    stock.fill = basic.paper;
    stock.stroke = basic.dark_border;
    stock.stroke_width = 1.5;
    if (state == CanvasPortState::hot) stock.fill = basic.accent_light;
    if (state == CanvasPortState::drop_accepted) stock.fill = stock_success;
    if (state == CanvasPortState::drop_accepted) stock.fill_class = detail::DarkColourClass::signal;
    if (state == CanvasPortState::drop_rejected) {
        stock.fill = stock_signal_red_pale;
        stock.stroke = stock_signal_red_deep;
        stock.fill_class = detail::DarkColourClass::signal_tint;
        stock.stroke_class = detail::DarkColourClass::signal;
    }
    return stock;
}

[[nodiscard]] CanvasLineStock link_stock(const BasicControlStyle& basic, CanvasLinkState state,
                                         CanvasLinkStyle style) noexcept {
    CanvasLineStock stock{};
    stock.colour = basic.dark_border;
    stock.width = 2.0;
    if (style == CanvasLinkStyle::dashed) stock.dash = 6.0;
    if (style == CanvasLinkStyle::dashed) stock.gap = 4.0;
    if (state == CanvasLinkState::hot) {
        stock.colour = basic.accent;
        stock.width = 2.5;
        stock.colour_class = detail::DarkColourClass::cue;
    }
    if (state == CanvasLinkState::selected) {
        stock.colour = basic.accent;
        stock.colour_class = detail::DarkColourClass::cue;
        stock.halo = with_alpha(basic.accent, 77);
        stock.halo_width = 4.0;
    }
    if (state == CanvasLinkState::invalid) {
        stock.colour = stock_signal_red;
        stock.colour_class = detail::DarkColourClass::signal;
        stock.dash = 2.0;
        stock.gap = 3.0;
    }
    return stock;
}

[[nodiscard]] CanvasAreaStock lamp_stock(const BasicControlStyle& basic, CanvasLampState state) noexcept {
    CanvasAreaStock stock{};
    stock.fill = basic.face;
    stock.stroke = basic.dark_border;
    stock.stroke_width = 1.0;
    // A lamp's colours are its state: they darken as signals, keeping
    // their hue (an unlit lamp's grey fill darkens as an unlit face).
    stock.fill_class = detail::DarkColourClass::signal;
    if (state == CanvasLampState::on) stock.fill = stock_success_light;
    if (state == CanvasLampState::on) stock.stroke = stock_success;
    if (state == CanvasLampState::error) stock.fill = stock_lamp_error;
    if (state == CanvasLampState::error) stock.stroke = stock_signal_red_deep;
    if (state != CanvasLampState::off) stock.stroke_class = detail::DarkColourClass::signal;
    return stock;
}

// A link's line in forced high contrast: the state's edge, a two-unit halo
// in the focus colour on the selected link only.
void apply_high_contrast_line(CanvasLine& line, CanvasLinkState state) noexcept {
    const bool selected = state == CanvasLinkState::selected;
    line.halo_width = selected ? 2.0 : 0.0;
    if (!selected) line.halo = clear_colour;
}

[[nodiscard]] double finite_or_zero(double value) noexcept {
    if (!std::isfinite(value)) return 0.0;
    return value;
}

// `rect` grown by `amount` on every side.
[[nodiscard]] Rect outset_rect(Rect rect, double amount) noexcept {
    const double twice = 2.0 * amount;
    const Rect result{rect.x - amount, rect.y - amount, rect.width + twice, rect.height + twice};
    return result;
}

// A filled and outlined disc of `radius` at `centre`.
void paint_canvas_disc(Painter& painter, const CanvasArea& area, Point centre, double radius) {
    if (!std::isfinite(radius) || !(radius > 0.0)) return;
    if (!std::isfinite(centre.x) || !std::isfinite(centre.y)) return;
    const double diameter = 2.0 * radius;
    const Rect disc{centre.x - radius, centre.y - radius, diameter, diameter};
    if (area.fill.alpha > 0U) painter.fill_rounded_rect(disc, radius, area.fill);
    const double width = std::min(area.stroke_width, radius);
    if (area.stroke.alpha == 0U || !(width > 0.0)) return;
    const double half = width / 2.0;
    const Rect ring = outset_rect(disc, -half);
    const double ring_radius = radius - half;
    painter.stroke_rounded_rect(ring, ring_radius, area.stroke, width);
}

// A canvas line's stroke style: round joins and caps, its dashes where it
// has a dash (a dash with no gap is solid).
[[nodiscard]] StrokeStyle canvas_stroke(Color colour, double width, double dash, double gap) noexcept {
    StrokeStyle style{};
    style.color = colour;
    style.width = width;
    style.join = StrokeJoin::round;
    style.cap = StrokeCap::round;
    const bool dashed = dash > 0.0 && gap > 0.0;
    if (dashed) {
        style.dashes[0] = dash;
        style.dashes[1] = gap;
        style.dash_count = 2U;
        style.cap = StrokeCap::butt;
    }
    return style;
}

} // namespace

const ControlAnatomy& node_canvas_anatomy() noexcept { return canvas_anatomy; }

double clamp_canvas_texture_contrast(double contrast) noexcept {
    if (!std::isfinite(contrast)) return 0.0;
    return std::clamp(contrast, 0.0, maximum_canvas_texture_contrast);
}

CanvasLook resolve_canvas_look(const Theme& theme, const Control& canvas, ControlVisualContext context) {
    const BasicControlStyle& basic = theme.basic_style();
    const ThemeDarkVariant dark_variant = theme.definition().dark_variant;
    CanvasColouring colouring{};
    colouring.high_contrast = context.high_contrast;
    const bool dark = context.dark && !context.high_contrast;
    colouring.darken_rules = dark && dark_variant == ThemeDarkVariant::derived;
    colouring.darken_stock = dark && dark_variant != ThemeDarkVariant::none;
    const CanvasStateRequest rest{};
    CanvasLook look{};

    // The region: the canvas colour, the grid's spacing and band, the
    // texture.
    const CanvasPartQuery region_query = canvas_query("region", ControlVisualRole::panel, context, rest);
    const CanvasPartPaint region = resolve_canvas_part(theme, canvas, region_query);
    CanvasAreaStock region_stock{};
    region_stock.fill = basic.paper;
    region_stock.stroke = clear_colour;
    region_stock.fill_class = detail::DarkColourClass::well_surface;
    look.background = canvas_area(region.canvas, region_stock, colouring);
    look.background.stroke = clear_colour;
    look.background.stroke_width = 0.0;
    look.grid.spacing = canvas_length(region.canvas.grid_spacing, 16.0);
    look.grid.spacing = std::clamp(look.grid.spacing, minimum_canvas_grid_spacing, maximum_canvas_grid_spacing);
    double band = canvas_length(region.canvas.grid_band, 6.0);
    band = std::clamp(band, minimum_canvas_grid_band, maximum_canvas_grid_band);
    look.grid.band.minimum_spacing = band;
    look.grid.band.maximum_spacing = band * grid_level_ratio;
    if (region.canvas.texture.has_value() && !context.high_contrast) {
        // Each layer whose clamped contrast is above 0, in order.
        const CanvasTextureLayers& authored = *region.canvas.texture;
        const std::size_t authored_count = std::min(authored.count, authored.items.size());
        for (std::size_t index = 0U; index < authored_count; ++index) {
            CanvasTextureLayer layer = authored.items[index];
            layer.contrast = clamp_canvas_texture_contrast(layer.contrast);
            if (!(layer.contrast > 0.0)) continue;
            look.texture.items[look.texture.count] = layer;
            look.texture.count = look.texture.count + 1U;
        }
    }
    if (dark) look.texture_blend = CanvasTextureBlend::screen;

    // The grid levels: the minor line's colour and width (both levels'),
    // the major line's colour.
    const CanvasPartQuery minor_query = canvas_query("grid_minor", ControlVisualRole::panel, context, rest);
    const CanvasPartPaint minor = resolve_canvas_part(theme, canvas, minor_query);
    const CanvasPartQuery major_query = canvas_query("grid_major", ControlVisualRole::panel, context, rest);
    const CanvasPartPaint major = resolve_canvas_part(theme, canvas, major_query);
    look.grid.minor =
        canvas_colour(minor.canvas.stroke, with_alpha(basic.accent, 40), detail::DarkColourClass::line, colouring);
    look.grid.major =
        canvas_colour(major.canvas.stroke, with_alpha(basic.accent, 96), detail::DarkColourClass::line, colouring);
    look.grid.line_width = canvas_length(minor.canvas.stroke_width, 1.0);
    if (!(look.grid.line_width > 0.0)) look.grid.line_width = 1.0;
    look.grid.minor_shown = !context.high_contrast;

    // Nodes and their headers, by node state.
    for (std::size_t index = 0U; index < canvas_node_state_count; ++index) {
        const CanvasNodeState state = static_cast<CanvasNodeState>(index);
        const CanvasStateRequest request = node_state_request(index);
        const CanvasPartQuery node_query = canvas_query("node", ControlVisualRole::card, context, request);
        const CanvasPartPaint node = resolve_canvas_part(theme, canvas, node_query);
        look.nodes[index] = canvas_area(node.canvas, node_stock(basic, state), colouring);
        const CanvasPartQuery header_query = canvas_query("node_header", ControlVisualRole::card, context, request);
        const CanvasPartPaint header = resolve_canvas_part(theme, canvas, header_query);
        look.headers[index] = canvas_area(header.canvas, header_stock(basic, state), colouring);
        // The engine has already resolved the title ink for the scheme.
        look.titles[index] = header.text;
    }

    // The selection ring.
    const CanvasPartQuery selection_query =
        canvas_query("node_selection", ControlVisualRole::selection, context, rest);
    const CanvasPartPaint selection = resolve_canvas_part(theme, canvas, selection_query);
    CanvasAreaStock selection_stock{};
    selection_stock.fill = clear_colour;
    selection_stock.stroke = basic.accent;
    selection_stock.stroke_width = 2.0;
    selection_stock.corner_radius = 8.0;
    selection_stock.stroke_class = detail::DarkColourClass::cue;
    look.selection = canvas_area(selection.canvas, selection_stock, colouring);
    if (context.high_contrast) look.selection.fill = clear_colour;

    // Ports, by port state.
    for (std::size_t index = 0U; index < canvas_port_state_count; ++index) {
        const CanvasPortState state = static_cast<CanvasPortState>(index);
        const CanvasStateRequest request = port_state_request(index);
        const CanvasPartQuery port_query = canvas_query("port", ControlVisualRole::button, context, request);
        const CanvasPartPaint port = resolve_canvas_part(theme, canvas, port_query);
        look.ports[index] = canvas_area(port.canvas, port_stock(basic, state), colouring);
    }

    // Links, plain and dashed, by link state; the flow from the plain link
    // at rest.
    for (std::size_t index = 0U; index < canvas_link_state_count; ++index) {
        const CanvasLinkState state = static_cast<CanvasLinkState>(index);
        const CanvasStateRequest request = link_state_request(index);
        const CanvasPartQuery link_query = canvas_query("link", ControlVisualRole::panel, context, request);
        const CanvasPartPaint link = resolve_canvas_part(theme, canvas, link_query);
        look.links[index] = canvas_line(link.canvas, link_stock(basic, state, CanvasLinkStyle::plain), colouring);
        const CanvasPartQuery dashed_query = canvas_query("dashed_link", ControlVisualRole::panel, context, request);
        const CanvasPartPaint dashed = resolve_canvas_part(theme, canvas, dashed_query);
        look.dashed_links[index] =
            canvas_line(dashed.canvas, link_stock(basic, state, CanvasLinkStyle::dashed), colouring);
        if (context.high_contrast) apply_high_contrast_line(look.links[index], state);
        if (context.high_contrast) apply_high_contrast_line(look.dashed_links[index], state);
        if (state != CanvasLinkState::normal) continue;
        if (link.canvas.flow.has_value()) look.flow.style = *link.canvas.flow;
        look.flow.speed = canvas_length(link.canvas.flow_speed, 48.0);
        look.flow.speed = std::min(look.flow.speed, maximum_canvas_flow_speed);
        look.flow.spacing = canvas_length(link.canvas.flow_spacing, 16.0);
        look.flow.spacing =
            std::clamp(look.flow.spacing, minimum_canvas_flow_spacing, maximum_canvas_flow_spacing);
    }

    // Lamps, by lamp state.
    for (std::size_t index = 0U; index < canvas_lamp_state_count; ++index) {
        const CanvasLampState state = static_cast<CanvasLampState>(index);
        const CanvasStateRequest request = lamp_state_request(index);
        const CanvasPartQuery lamp_query = canvas_query("lamp", ControlVisualRole::progress, context, request);
        const CanvasPartPaint lamp = resolve_canvas_part(theme, canvas, lamp_query);
        look.lamps[index] = canvas_area(lamp.canvas, lamp_stock(basic, state), colouring);
    }

    // The marquee: its area and its dashed outline.
    const CanvasPartQuery marquee_query = canvas_query("marquee", ControlVisualRole::selection, context, rest);
    const CanvasPartPaint marquee = resolve_canvas_part(theme, canvas, marquee_query);
    CanvasAreaStock marquee_stock{};
    marquee_stock.fill = with_alpha(basic.accent, 31);
    marquee_stock.stroke = basic.accent;
    marquee_stock.stroke_width = 1.0;
    marquee_stock.fill_class = detail::DarkColourClass::cue;
    marquee_stock.stroke_class = detail::DarkColourClass::cue;
    look.marquee = canvas_area(marquee.canvas, marquee_stock, colouring);
    if (context.high_contrast) look.marquee.fill = clear_colour;
    look.marquee_outline.colour = look.marquee.stroke;
    look.marquee_outline.width = look.marquee.stroke_width;
    look.marquee_outline.dash = canvas_length(marquee.canvas.dash, 4.0);
    look.marquee_outline.gap = canvas_length(marquee.canvas.gap, 3.0);
    return look;
}

ProceduralFill canvas_texture_fill(const CanvasTextureLayer& layer, CanvasTextureBlend blend) noexcept {
    const bool screened = blend == CanvasTextureBlend::screen;
    const Color white = Color::rgba(255, 255, 255);
    const Color black = Color::rgba(0, 0, 0);
    ProceduralFill fill{};
    fill.pattern = layer.pattern;
    fill.seed = layer.seed;
    fill.tile = layer.tile;
    fill.strength = layer.strength;
    fill.ground = screened ? black : white;
    fill.figure = screened ? white : black;
    fill.accent = fill.ground;
    return fill;
}

void paint_canvas_background(Painter& painter, const Control& canvas, const CanvasLook& look, Rect area,
                             CanvasView view) {
    if (area.empty()) return;
    if (look.background.fill.alpha > 0U) painter.fill_rect(area, look.background.fill);
    const bool placed = std::isfinite(view.origin.x) && std::isfinite(view.origin.y) && std::isfinite(view.zoom) &&
                        view.zoom > 0.0;
    const std::size_t layer_count = std::min(look.texture.count, look.texture.items.size());
    if (placed && layer_count > 0U) {
        // One material, reused for each layer: its one fill is replaced in
        // place, so the vector allocates once per paint.
        const ThemeEngine& engine = canvas.effective_theme().engine();
        const bool screened = look.texture_blend == CanvasTextureBlend::screen;
        constexpr std::array<std::string_view, maximum_canvas_texture_layers> layer_keys{"texture.0", "texture.1",
                                                                                          "texture.2"};
        SurfaceMaterial material{};
        for (std::size_t index = 0U; index < layer_count; ++index) {
            const CanvasTextureLayer& layer = look.texture.items[index];
            const double contrast = clamp_canvas_texture_contrast(layer.contrast);
            if (!(contrast > 0.0)) continue;
            material.fills.assign(1U, MaterialFillLayer::procedural_pattern(canvas_texture_fill(layer,
                                                                                                 look.texture_blend)));
            MaterialBlocks blocks{};
            blocks.key = PartKey{layer_keys[index], {}, {}};
            blocks.material = &material;
            blocks.period = layer.tile;
            blocks.anchor = view.origin;
            blocks.area = area;
            PaintGroup group{};
            group.bounds = area;
            group.opacity = contrast;
            group.composite = screened ? PaintGroupComposite::screen : PaintGroupComposite::multiply;
            group.isolated = true;
            painter.begin_group(group);
            static_cast<void>(engine.paint_material_blocks(painter, canvas, blocks));
            painter.end_group();
        }
    }
    if (!placed || !(look.grid.spacing > 0.0)) return;
    const GridLevel level = select_grid_level(look.grid.spacing, view.zoom, look.grid.band);
    if (!(level.minor_spacing >= minimum_grid_spacing)) return;
    GridPattern pattern = grid_pattern_at_level(level, view.origin, look.grid.minor, look.grid.major,
                                                look.grid.line_width);
    if (!look.grid.minor_shown) pattern.minor = clear_colour;
    painter.fill_grid_pattern(area, pattern);
}

void paint_canvas_node(Painter& painter, const CanvasLook& look, Rect body, double header_height,
                       CanvasNodeState state) {
    const std::size_t index = static_cast<std::size_t>(state);
    if (index >= canvas_node_state_count || body.empty()) return;
    const CanvasArea& node = look.nodes[index];
    const CanvasArea& header = look.headers[index];
    const double shortest = std::min(body.width, body.height);
    const double radius = std::min(node.corner_radius, shortest / 2.0);
    if (node.fill.alpha > 0U) painter.fill_rounded_rect(body, radius, node.fill);
    const double band = std::clamp(finite_or_zero(header_height), 0.0, body.height);
    if (band > 0.0) {
        const double header_radius = std::min(header.corner_radius, shortest / 2.0);
        const Rect header_band{body.x, body.y, body.width, band};
        painter.save();
        painter.clip_rect(header_band);
        if (header.fill.alpha > 0U) painter.fill_rounded_rect(body, header_radius, header.fill);
        painter.restore();
        const bool ruled = header.stroke.alpha > 0U && header.stroke_width > 0.0 && band < body.height;
        if (ruled) {
            const double rule_y = body.y + band;
            const Point from{body.x, rule_y};
            const Point to{body.right(), rule_y};
            painter.draw_line(from, to, header.stroke, header.stroke_width);
        }
    }
    const double width = std::min(node.stroke_width, shortest / 2.0);
    if (node.stroke.alpha == 0U || !(width > 0.0)) return;
    const double half = width / 2.0;
    const Rect outline = outset_rect(body, -half);
    const double outline_radius = std::max(radius - half, 0.0);
    painter.stroke_rounded_rect(outline, outline_radius, node.stroke, width);
}

void paint_canvas_node_selection(Painter& painter, const CanvasLook& look, Rect body) {
    const CanvasArea& ring = look.selection;
    if (body.empty() || ring.stroke.alpha == 0U || !(ring.stroke_width > 0.0)) return;
    const double reach = 2.0 + ring.stroke_width / 2.0;
    const Rect around = outset_rect(body, reach);
    painter.stroke_rounded_rect(around, ring.corner_radius, ring.stroke, ring.stroke_width);
}

void paint_canvas_port(Painter& painter, const CanvasLook& look, Point centre, double radius, CanvasPortState state) {
    const std::size_t index = static_cast<std::size_t>(state);
    if (index >= canvas_port_state_count) return;
    paint_canvas_disc(painter, look.ports[index], centre, radius);
}

void paint_canvas_link(Painter& painter, const CanvasLook& look, std::span<const Point> points, CanvasLinkState state,
                       CanvasLinkStyle style) {
    const std::size_t index = static_cast<std::size_t>(state);
    if (index >= canvas_link_state_count || points.size() < 2U) return;
    const bool dashed = style == CanvasLinkStyle::dashed;
    const CanvasLine& line = dashed ? look.dashed_links[index] : look.links[index];
    const bool haloed = line.halo.alpha > 0U && line.halo_width > 0.0;
    if (haloed) {
        const double halo_width = line.width + 2.0 * line.halo_width;
        const StrokeStyle halo = canvas_stroke(line.halo, halo_width, 0.0, 0.0);
        painter.stroke_polyline(points, halo);
    }
    if (line.colour.alpha == 0U || !(line.width > 0.0)) return;
    const StrokeStyle stroke = canvas_stroke(line.colour, line.width, line.dash, line.gap);
    painter.stroke_polyline(points, stroke);
}

void paint_canvas_lamp(Painter& painter, const CanvasLook& look, Point centre, double radius, CanvasLampState state) {
    const std::size_t index = static_cast<std::size_t>(state);
    if (index >= canvas_lamp_state_count) return;
    paint_canvas_disc(painter, look.lamps[index], centre, radius);
}

void paint_canvas_marquee(Painter& painter, const CanvasLook& look, Rect rect) {
    if (rect.empty()) return;
    if (look.marquee.fill.alpha > 0U) painter.fill_rect(rect, look.marquee.fill);
    const CanvasLine& outline = look.marquee_outline;
    if (outline.colour.alpha == 0U || !(outline.width > 0.0)) return;
    const double half = outline.width / 2.0;
    const Rect inner = outset_rect(rect, -half);
    const std::array<Point, 5> corners{Point{inner.x, inner.y}, Point{inner.right(), inner.y},
                                       Point{inner.right(), inner.bottom()}, Point{inner.x, inner.bottom()},
                                       Point{inner.x, inner.y}};
    StrokeStyle style = canvas_stroke(outline.colour, outline.width, outline.dash, outline.gap);
    style.join = StrokeJoin::miter;
    painter.stroke_polyline(corners, style);
}

} // namespace gui_forms
