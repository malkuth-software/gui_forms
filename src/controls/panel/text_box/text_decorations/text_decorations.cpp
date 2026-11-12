#include "gui_forms/controls/text_decorations.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

constexpr std::uint16_t minimum_weight = 1U;
constexpr std::uint16_t maximum_weight = 1000U;

// Refuses a decoration with end before start or a weight out of range;
// `within`, when given, must contain the range.
void validate(const TextDecoration& decoration, const std::optional<Utf8Range> within) {
    if (decoration.range.end < decoration.range.start) {
        throw std::invalid_argument("A text decoration's range must not end before it starts");
    }
    if (decoration.style.weight.has_value() &&
        (*decoration.style.weight < minimum_weight || *decoration.style.weight > maximum_weight)) {
        throw std::invalid_argument("A text decoration's weight must lie in 1 to 1000");
    }
    if (within.has_value() &&
        (decoration.range.start < (*within).start || decoration.range.end > (*within).end)) {
        throw std::invalid_argument("A replaced text decoration must lie inside the replaced range");
    }
}

bool decoration_is_empty(const TextDecoration& decoration) noexcept {
    const bool result = decoration.range.empty();
    return result;
}

struct DecorationIsEmpty final {
    bool operator()(const TextDecoration& decoration) const noexcept {
        const bool result = decoration_is_empty(decoration);
        return result;
    }
};

// Lays `style`'s set fields over `composed`.
void overlay(TextDecorationStyle& composed, const TextDecorationStyle& style) noexcept {
    if (style.ink.has_value()) composed.ink = style.ink;
    if (style.foreground.has_value()) composed.foreground = style.foreground;
    if (style.weight.has_value()) composed.weight = style.weight;
    if (style.italic.has_value()) composed.italic = style.italic;
    if (style.underline.has_value()) composed.underline = style.underline;
    if (style.underline_color.has_value()) composed.underline_color = style.underline_color;
    if (style.background.has_value()) composed.background = style.background;
}

// The extent of `items` (the smallest range holding them all), or an empty
// range at `fallback` without any.
Utf8Range extent_of(std::span<const TextDecoration> items, Utf8Offset fallback) noexcept {
    if (items.empty()) {
        const Utf8Range none{fallback, fallback};
        return none;
    }
    Utf8Offset first = items.front().range.start;
    Utf8Offset last = items.front().range.end;
    for (const TextDecoration& item : items) {
        first = std::min(first, item.range.start);
        last = std::max(last, item.range.end);
    }
    const Utf8Range extent{first, last};
    return extent;
}

Utf8Range union_of(Utf8Range left, Utf8Range right) noexcept {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const Utf8Range joined{std::min(left.start, right.start), std::max(left.end, right.end)};
    return joined;
}

bool any_changes_font(std::span<const TextDecoration> items) noexcept {
    for (const TextDecoration& item : items) {
        if (item.style.changes_font()) return true;
    }
    return false;
}

} // namespace

TextChange text_change_between(std::string_view before, std::string_view after) noexcept {
    const std::size_t shorter = std::min(before.size(), after.size());
    std::size_t prefix = 0U;
    while (prefix < shorter && before[prefix] == after[prefix]) ++prefix;
    // The suffix may not reach back into the prefix of either text.
    const std::size_t suffix_limit = shorter - prefix;
    std::size_t suffix = 0U;
    while (suffix < suffix_limit &&
           before[before.size() - 1U - suffix] == after[after.size() - 1U - suffix]) {
        ++suffix;
    }
    const TextChange change{.start = Utf8Offset(prefix),
                            .removed_bytes = before.size() - prefix - suffix,
                            .inserted_bytes = after.size() - prefix - suffix};
    return change;
}

bool TextDecorations::DecorationStartsBefore::operator()(const TextDecoration& left,
                                                          const TextDecoration& right) const noexcept {
    const bool result = left.range.start < right.range.start;
    return result;
}

bool TextDecorations::LayerBefore::operator()(const Layer& layer, TextDecorationLayer id) const noexcept {
    const bool result = layer.id < id;
    return result;
}

std::size_t TextDecorations::find_layer(TextDecorationLayer id) const noexcept {
    const std::vector<Layer>::const_iterator found =
        std::lower_bound(layers_.cbegin(), layers_.cend(), id, LayerBefore{});
    const std::size_t index = static_cast<std::size_t>(found - layers_.cbegin());
    return index;
}

TextDecorationLayer TextDecorations::layer_at(std::size_t index) const {
    if (index >= layers_.size()) throw std::out_of_range("No text decoration layer at that index");
    const TextDecorationLayer id = layers_[index].id;
    return id;
}

std::span<const TextDecoration> TextDecorations::items(TextDecorationLayer layer) const noexcept {
    const std::size_t index = find_layer(layer);
    if (index == layers_.size() || layers_[index].id != layer) return {};
    const std::span<const TextDecoration> result(layers_[index].items);
    return result;
}

void TextDecorations::measure_layer(Layer& layer) noexcept {
    layer.longest = 0U;
    layer.font_count = 0U;
    for (const TextDecoration& item : layer.items) {
        const std::size_t length = item.range.end.value() - item.range.start.value();
        layer.longest = std::max(layer.longest, length);
        if (item.style.changes_font()) ++layer.font_count;
    }
}

void TextDecorations::install(TextDecorationLayer id, std::vector<TextDecoration> items, bool layout) {
    const std::size_t index = find_layer(id);
    const bool present = index != layers_.size() && layers_[index].id == id;
    if (present) {
        font_decorations_ -= layers_[index].font_count;
    }
    if (items.empty()) {
        if (present) {
            const std::vector<Layer>::iterator position =
                layers_.begin() + static_cast<std::ptrdiff_t>(index);
            layers_.erase(position);
        }
    } else {
        if (!present) {
            Layer created{};
            created.id = id;
            const std::vector<Layer>::iterator position =
                layers_.begin() + static_cast<std::ptrdiff_t>(index);
            layers_.insert(position, std::move(created));
        }
        Layer& target = layers_[index];
        target.items = std::move(items);
        measure_layer(target);
        font_decorations_ += target.font_count;
    }
    if (layout) ++layout_revision_;
}

TextDecorationChange TextDecorations::replace(TextDecorationLayer layer,
                                              std::span<const TextDecoration> decorations) {
    for (const TextDecoration& decoration : decorations) {
        validate(decoration, std::nullopt);
    }
    std::vector<TextDecoration> kept{};
    kept.reserve(decorations.size());
    for (const TextDecoration& decoration : decorations) {
        if (!decoration_is_empty(decoration)) kept.push_back(decoration);
    }
    std::stable_sort(kept.begin(), kept.end(), DecorationStartsBefore{});
    const std::span<const TextDecoration> previous = items(layer);
    TextDecorationChange change{};
    const bool same = previous.size() == kept.size() && std::equal(previous.begin(), previous.end(), kept.begin());
    if (same) return change;
    const std::span<const TextDecoration> incoming(kept);
    change.range = union_of(extent_of(previous, Utf8Offset{}), extent_of(incoming, Utf8Offset{}));
    change.changed = true;
    change.layout = any_changes_font(previous) || any_changes_font(incoming);
    install(layer, std::move(kept), change.layout);
    return change;
}

TextDecorationChange TextDecorations::replace_range(TextDecorationLayer layer, Utf8Range range,
                                                    std::span<const TextDecoration> decorations) {
    if (range.end < range.start) {
        throw std::invalid_argument("A replaced text decoration range must not end before it starts");
    }
    for (const TextDecoration& decoration : decorations) {
        validate(decoration, range);
    }
    std::vector<TextDecoration> incoming{};
    incoming.reserve(decorations.size());
    for (const TextDecoration& decoration : decorations) {
        if (!decoration_is_empty(decoration)) incoming.push_back(decoration);
    }
    std::stable_sort(incoming.begin(), incoming.end(), DecorationStartsBefore{});

    // The range as it looks now: each decoration reaching into it, cut to it.
    const std::span<const TextDecoration> previous = items(layer);
    std::vector<TextDecoration> inside{};
    bool previous_font = false;
    for (const TextDecoration& item : previous) {
        const bool overlaps = item.range.start < range.end && item.range.end > range.start;
        if (!overlaps) continue;
        TextDecoration piece = item;
        piece.range.start = std::max(item.range.start, range.start);
        piece.range.end = std::min(item.range.end, range.end);
        if (item.style.changes_font()) previous_font = true;
        inside.push_back(piece);
    }
    std::stable_sort(inside.begin(), inside.end(), DecorationStartsBefore{});
    TextDecorationChange change{};
    const bool same = inside.size() == incoming.size() && std::equal(inside.begin(), inside.end(), incoming.begin());
    if (same) return change;

    // Decorations away from the range stay; those crossing it keep their
    // outer parts.
    std::vector<TextDecoration> result{};
    result.reserve(previous.size() + incoming.size() + 1U);
    for (const TextDecoration& item : previous) {
        const bool overlaps = item.range.start < range.end && item.range.end > range.start;
        if (!overlaps) {
            result.push_back(item);
            continue;
        }
        if (item.range.start < range.start) {
            TextDecoration before = item;
            before.range.end = range.start;
            result.push_back(before);
        }
        if (item.range.end > range.end) {
            TextDecoration after = item;
            after.range.start = range.end;
            result.push_back(after);
        }
    }
    for (const TextDecoration& decoration : incoming) {
        result.push_back(decoration);
    }
    std::stable_sort(result.begin(), result.end(), DecorationStartsBefore{});
    change.range = range;
    change.changed = true;
    change.layout = previous_font || any_changes_font(incoming);
    install(layer, std::move(result), change.layout);
    return change;
}

TextDecorationChange TextDecorations::clear(TextDecorationLayer layer) {
    const std::span<const TextDecoration> none{};
    const TextDecorationChange change = replace(layer, none);
    return change;
}

TextDecorationChange TextDecorations::clear_all() {
    TextDecorationChange change{};
    for (const Layer& layer : layers_) {
        const std::span<const TextDecoration> layer_items(layer.items);
        change.range = union_of(change.range, extent_of(layer_items, Utf8Offset{}));
        change.changed = true;
        if (layer.font_count != 0U) change.layout = true;
    }
    layers_.clear();
    font_decorations_ = 0U;
    if (change.layout) ++layout_revision_;
    return change;
}

void TextDecorations::shift_for_edit(TextChange change) {
    const std::size_t edit_start = change.start.value();
    const std::size_t old_end = edit_start + change.removed_bytes;
    const std::size_t new_end = edit_start + change.inserted_bytes;
    bool emptied_layer = false;
    for (Layer& layer : layers_) {
        for (TextDecoration& decoration : layer.items) {
            std::size_t start = decoration.range.start.value();
            std::size_t end = decoration.range.end.value();
            const bool removed_entirely = start >= edit_start && end <= old_end && change.removed_bytes > 0U;
            if (removed_entirely) {
                decoration.range.end = decoration.range.start;  // dropped below
                continue;
            }
            if (start >= old_end) {
                start = start - old_end + new_end;
            } else if (start > edit_start) {
                start = edit_start;
            }
            if (end >= old_end) {
                end = end - old_end + new_end;
            } else if (end > edit_start) {
                end = new_end;
            }
            decoration.range = Utf8Range{Utf8Offset(start), Utf8Offset(end)};
        }
        const std::vector<TextDecoration>::iterator kept =
            std::remove_if(layer.items.begin(), layer.items.end(), DecorationIsEmpty{});
        layer.items.erase(kept, layer.items.end());
        // A range pulled back to the edit's start may now start before one
        // that started earlier inside the removed bytes.
        std::stable_sort(layer.items.begin(), layer.items.end(), DecorationStartsBefore{});
        font_decorations_ -= layer.font_count;
        measure_layer(layer);
        font_decorations_ += layer.font_count;
        if (layer.items.empty()) emptied_layer = true;
    }
    if (emptied_layer) {
        std::vector<Layer>::iterator kept = layers_.begin();
        for (std::vector<Layer>::iterator layer = layers_.begin(); layer != layers_.end(); ++layer) {
            if ((*layer).items.empty()) continue;
            if (kept != layer) *kept = std::move(*layer);
            ++kept;
        }
        layers_.erase(kept, layers_.end());
    }
}

void TextDecorations::collect_overlapping(const Layer& layer, Utf8Range window) {
    // Decorations are in start order and none is longer than `longest`, so
    // only those starting in [window.start - longest, window.end) can reach
    // the window.
    const std::size_t reach = window.start.value() > layer.longest ? window.start.value() - layer.longest : 0U;
    TextDecoration probe{};
    probe.range.start = Utf8Offset(reach);
    const std::vector<TextDecoration>::const_iterator first =
        std::lower_bound(layer.items.cbegin(), layer.items.cend(), probe, DecorationStartsBefore{});
    for (std::vector<TextDecoration>::const_iterator item = first; item != layer.items.cend(); ++item) {
        if (!((*item).range.start < window.end)) break;
        if ((*item).range.end > window.start) overlapping_.push_back(&*item);
    }
}

void TextDecorations::compose(Utf8Range window, std::vector<StyledTextRun>& runs) {
    runs.clear();
    if (!(window.start < window.end)) return;
    overlapping_.clear();
    for (const Layer& layer : layers_) {
        collect_overlapping(layer, window);
    }
    if (overlapping_.empty()) {
        const StyledTextRun plain{.range = window, .style = {}};
        runs.push_back(plain);
        return;
    }
    cuts_.clear();
    cuts_.push_back(window.start.value());
    cuts_.push_back(window.end.value());
    for (const TextDecoration* decoration : overlapping_) {
        const std::size_t start = std::max((*decoration).range.start.value(), window.start.value());
        const std::size_t end = std::min((*decoration).range.end.value(), window.end.value());
        cuts_.push_back(start);
        cuts_.push_back(end);
    }
    std::sort(cuts_.begin(), cuts_.end());
    const std::vector<std::size_t>::iterator unique_end = std::unique(cuts_.begin(), cuts_.end());
    cuts_.erase(unique_end, cuts_.end());
    // Each segment between cuts is covered by a fixed set of decorations;
    // overlapping_ is in layer order and start order within a layer, so
    // overlaying in that order lets the later one stand.
    for (std::size_t cut = 0U; cut + 1U < cuts_.size(); ++cut) {
        const std::size_t start = cuts_[cut];
        const std::size_t end = cuts_[cut + 1U];
        TextDecorationStyle composed{};
        for (const TextDecoration* decoration : overlapping_) {
            const bool covers = (*decoration).range.start.value() <= start && (*decoration).range.end.value() >= end;
            if (covers) overlay(composed, (*decoration).style);
        }
        if (!runs.empty() && runs.back().style == composed) {
            runs.back().range.end = Utf8Offset(end);
            continue;
        }
        const StyledTextRun run{.range = Utf8Range{Utf8Offset(start), Utf8Offset(end)}, .style = composed};
        runs.push_back(run);
    }
    overlapping_.clear();
}

} // namespace gui_forms
