#include "../support/drawing_support.hpp"

Bitmap::Bitmap(std::uint32_t width, std::uint32_t height,
               PixelFormat pixel_format) {
    require_enum(pixel_format, 0U, "bitmap pixel format");
    if (width == 0U || height == 0U || width > maximum_dimension ||
        height > maximum_dimension) {
        throw std::invalid_argument("bitmap dimensions must be nonzero and bounded");
    }
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4ULL;
    const std::uint64_t byte_count = row_bytes * height;
    if (byte_count > maximum_bytes ||
        byte_count > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("bitmap byte limit exceeded");
    }
    storage_ = std::make_shared<PixelStorage>();
    (*storage_).width = width;
    (*storage_).height = height;
    (*storage_).pixel_format = pixel_format;
    (*storage_).row_bytes = static_cast<std::size_t>(row_bytes);
    (*storage_).bytes.resize(static_cast<std::size_t>(byte_count));
    damage_history_.reserve(maximum_damage_history);
    stable_id_ = next_bitmap_id.fetch_add(1U, std::memory_order_relaxed);
    if (stable_id_ == 0U) {
        throw std::overflow_error("bitmap identity space exhausted");
    }
}

std::unique_ptr<Bitmap> Bitmap::borrow(std::uint32_t width, std::uint32_t height,
                                       std::size_t row_bytes, std::byte* memory) {
    if (memory == nullptr) throw std::invalid_argument("a borrowing bitmap needs memory");
    if (width == 0U || height == 0U || width > maximum_dimension ||
        height > maximum_dimension) {
        throw std::invalid_argument("bitmap dimensions must be nonzero and bounded");
    }
    if (row_bytes < static_cast<std::size_t>(width) * 4U || row_bytes % 4U != 0U ||
        row_bytes > std::numeric_limits<std::size_t>::max() / height) {
        throw std::invalid_argument("a borrowing bitmap's row bytes must hold its row");
    }
    // Made at 1 x 1 (a one-pixel owned buffer, released below), then
    // pointed at the borrowed pixels: the constructor keeps the identity and
    // damage bookkeeping in one place.
    std::unique_ptr<Bitmap> result = std::make_unique<Bitmap>(1U, 1U, PixelFormat::bgra32_premultiplied);
    PixelStorage& storage = *(*result).storage_;
    storage.bytes.clear();
    storage.bytes.shrink_to_fit();
    storage.width = width;
    storage.height = height;
    storage.row_bytes = row_bytes;
    storage.borrowed = memory;
    return result;
}

bool Bitmap::borrows_memory() const {
    require_alive();
    return (*storage_).borrowed != nullptr;
}

const std::byte* Bitmap::borrowed_memory() const {
    require_alive();
    return (*storage_).borrowed;
}

std::uint32_t Bitmap::width() const {
    require_alive();
    return (*storage_).width;
}

std::uint32_t Bitmap::height() const {
    require_alive();
    return (*storage_).height;
}

PixelFormat Bitmap::pixel_format() const {
    require_alive();
    return (*storage_).pixel_format;
}

std::uint64_t Bitmap::generation() const {
    require_alive();
    return generation_;
}

Color Bitmap::get_pixel(std::uint32_t x, std::uint32_t y) const {
    require_alive();
    require_unlocked();
    require_coordinate(x, y);
    return load_color(*storage_, x, y);
}

void Bitmap::set_pixel(std::uint32_t x, std::uint32_t y, Color color) {
    require_alive();
    require_unlocked();
    require_coordinate(x, y);
    if (stored_color_equals(*storage_, x, y, color)) return;
    prepare_write();
    store_color(*storage_, x, y, color);
    publish_mutation({RectI{static_cast<std::int32_t>(x),
                            static_cast<std::int32_t>(y), 1, 1}});
}

void Bitmap::make_transparent(Color key) {
    require_alive();
    require_unlocked();
    bool found = false;
    for (std::uint32_t y = 0; y < (*storage_).height && !found; ++y) {
        for (std::uint32_t x = 0; x < (*storage_).width; ++x) {
            const Color value = load_color(*storage_, x, y);
            if (value.alpha() != 0U && value.red() == key.red() &&
                value.green() == key.green() &&
                value.blue() == key.blue()) {
                found = true;
                break;
            }
        }
    }
    if (!found) return;

    prepare_write();
    std::vector<RectI> damage;
    for (std::uint32_t y = 0; y < (*storage_).height; ++y) {
        std::optional<std::uint32_t> run_start;
        for (std::uint32_t x = 0; x < (*storage_).width; ++x) {
            const Color value = load_color(*storage_, x, y);
            if (value.alpha() != 0U && value.red() == key.red() &&
                value.green() == key.green() &&
                value.blue() == key.blue()) {
                store_color(*storage_, x, y, Color::from_argb(0U, 0U, 0U, 0U));
                if (!run_start) run_start = x;
            } else if (run_start) {
                damage.push_back({static_cast<std::int32_t>(*run_start),
                                  static_cast<std::int32_t>(y),
                                  static_cast<std::int32_t>(x - *run_start), 1});
                run_start.reset();
            }
        }
        if (run_start) {
            damage.push_back({static_cast<std::int32_t>(*run_start),
                              static_cast<std::int32_t>(y),
                              static_cast<std::int32_t>((*storage_).width - *run_start),
                              1});
        }
        if (damage.size() > maximum_damage_rectangles) {
            damage.assign(1U, RectI{0, 0,
                static_cast<std::int32_t>((*storage_).width),
                static_cast<std::int32_t>((*storage_).height)});
            for (++y; y < (*storage_).height; ++y) {
                for (std::uint32_t x = 0; x < (*storage_).width; ++x) {
                    const Color value = load_color(*storage_, x, y);
                    if (value.alpha() != 0U && value.red() == key.red() &&
                        value.green() == key.green() &&
                        value.blue() == key.blue()) {
                        store_color(*storage_, x, y,
                                    Color::from_argb(0U, 0U, 0U, 0U));
                    }
                }
            }
            break;
        }
    }
    publish_mutation(std::move(damage));
}

std::unique_ptr<Bitmap> Bitmap::clone(RectI source) const {
    require_alive();
    require_unlocked();
    if (source.width <= 0 || source.height <= 0 || source.x < 0 || source.y < 0 ||
        source.right() > (*storage_).width || source.bottom() > (*storage_).height) {
        throw std::invalid_argument("bitmap clone rectangle is outside the image");
    }
    std::unique_ptr<gui_drawing::Bitmap> result = std::make_unique<Bitmap>(static_cast<std::uint32_t>(source.width),
                                           static_cast<std::uint32_t>(source.height),
                                           (*storage_).pixel_format);
    for (std::int32_t y = 0; y < source.height; ++y) {
        const std::size_t input = pixel_offset(
            *storage_, static_cast<std::uint32_t>(source.x),
            static_cast<std::uint32_t>(source.y + y));
        const std::size_t output = static_cast<std::size_t>(y) * (*(*result).storage_).row_bytes;
        std::copy_n((*storage_).data() + input,
                    (*(*result).storage_).row_bytes,
                    (*(*result).storage_).data() + output);
    }
    return result;
}

std::unique_ptr<Bitmap> Bitmap::thumbnail(std::uint32_t width,
                                          std::uint32_t height) const {
    require_alive();
    require_unlocked();
    std::unique_ptr<gui_drawing::Bitmap> result = std::make_unique<Bitmap>(width, height, (*storage_).pixel_format);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t source_y = std::min(
            (*storage_).height - 1U,
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) *
                                        (*storage_).height) / height));
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t source_x = std::min(
                (*storage_).width - 1U,
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) *
                                            (*storage_).width) / width));
            store_color(*(*result).storage_, x, y,
                        load_color(*storage_, source_x, source_y));
        }
    }
    return result;
}

namespace {

// The colour the remap table maps `input` to: the first entry whose old
// colour matches, else `input` unchanged.
[[nodiscard]] Color remapped_color(const ImageAttributesSnapshot& adjustment, Color input) noexcept {
    std::vector<ImageAttributesSnapshot::ColorRemap>::const_iterator remap = adjustment.remap_table.begin();
    while (remap != adjustment.remap_table.end() && (*remap).old_color.argb() != input.argb()) {
        ++remap;
    }
    if (remap == adjustment.remap_table.end()) return input;
    return (*remap).new_color;
}

} // namespace

std::unique_ptr<Bitmap> Bitmap::adjusted(
    const ImageAttributes& attributes) const {
    require_alive();
    require_unlocked();
    const ImageAttributesSnapshot adjustment = attributes.snapshot();
    std::unique_ptr<gui_drawing::Bitmap> result = std::make_unique<Bitmap>((*storage_).width, (*storage_).height,
                                           (*storage_).pixel_format);
    // Without a colour matrix the adjustment is the remap alone.
    if (!adjustment.color_matrix.has_value()) {
        for (std::uint32_t y = 0; y < (*storage_).height; ++y) {
            for (std::uint32_t x = 0; x < (*storage_).width; ++x) {
                const Color remapped = remapped_color(adjustment, load_color(*storage_, x, y));
                store_color(*(*result).storage_, x, y, remapped);
            }
        }
        return result;
    }
    const std::array<double, 25>& matrix = *adjustment.color_matrix;
    for (std::uint32_t y = 0; y < (*storage_).height; ++y) {
        for (std::uint32_t x = 0; x < (*storage_).width; ++x) {
            const Color input = remapped_color(adjustment, load_color(*storage_, x, y));
            const double values[5] = {
                input.red() / 255.0, input.green() / 255.0,
                input.blue() / 255.0, input.alpha() / 255.0, 1.0};
            double output[4]{};
            for (std::size_t column = 0; column < 4U; ++column) {
                for (std::size_t row = 0; row < 5U; ++row) {
                    output[column] += values[row] * matrix[row * 5U + column];
                }
            }
            store_color(*(*result).storage_, x, y,
                        Color::from_argb(normalized_channel(output[3]),
                                         normalized_channel(output[0]),
                                         normalized_channel(output[1]),
                                         normalized_channel(output[2])));
        }
    }
    return result;
}

BitmapLockView Bitmap::lock(BitmapLockMode mode) {
    require_alive();
    require_enum(mode, 2U, "bitmap lock mode");
    require_unlocked();
    if (mode != BitmapLockMode::read) prepare_write();
    active_lock_token_ = next_lock_token_++;
    if (active_lock_token_ == 0U) {
        throw std::overflow_error("bitmap lock token space exhausted");
    }
    lock_mode_ = mode;
    return {(*storage_).data(),
            mode == BitmapLockMode::read ? nullptr : (*storage_).data(),
            (*storage_).row_bytes, (*storage_).width, (*storage_).height,
            (*storage_).pixel_format, active_lock_token_};
}

void Bitmap::unlock(std::uint64_t token) {
    require_alive();
    if (active_lock_token_ == 0U || token == 0U || token != active_lock_token_) {
        throw std::invalid_argument("bitmap lock token is invalid or already released");
    }
    if (active_edit_) {
        throw std::logic_error(
            "a bounded bitmap edit must be committed or cancelled explicitly");
    }
    const bool wrote = lock_mode_ != BitmapLockMode::read;
    active_lock_token_ = 0U;
    lock_mode_ = BitmapLockMode::read;
    if (wrote) {
        publish_mutation({RectI{0, 0,
            static_cast<std::int32_t>((*storage_).width),
            static_cast<std::int32_t>((*storage_).height)}});
    }
}

bool Bitmap::locked() const {
    require_alive();
    return active_lock_token_ != 0U;
}

BitmapEditView Bitmap::begin_edit(RectI bounds) {
    require_alive();
    require_unlocked();
    if (bounds.x < 0 || bounds.y < 0 || bounds.width <= 0 ||
        bounds.height <= 0 || bounds.right() > (*storage_).width ||
        bounds.bottom() > (*storage_).height) {
        throw std::invalid_argument(
            "bitmap edit rectangle must be nonempty and inside the image");
    }
    prepare_write();

    const std::size_t edit_row_bytes =
        static_cast<std::size_t>(bounds.width) * 4U;
    active_edit_backup_.resize(
        edit_row_bytes * static_cast<std::size_t>(bounds.height));
    for (std::int32_t row = 0; row < bounds.height; ++row) {
        const std::size_t source = pixel_offset(
            *storage_, static_cast<std::uint32_t>(bounds.x),
            static_cast<std::uint32_t>(bounds.y + row));
        std::copy_n((*storage_).data() + source, edit_row_bytes,
                    active_edit_backup_.data() +
                        static_cast<std::size_t>(row) * edit_row_bytes);
    }

    active_lock_token_ = next_lock_token_++;
    if (active_lock_token_ == 0U) {
        active_edit_backup_.clear();
        throw std::overflow_error("bitmap lock token space exhausted");
    }
    active_edit_ = true;
    active_edit_bounds_ = bounds;
    lock_mode_ = BitmapLockMode::read_write;
    const std::size_t origin = pixel_offset(
        *storage_, static_cast<std::uint32_t>(bounds.x),
        static_cast<std::uint32_t>(bounds.y));
    return {(*storage_).data() + origin, (*storage_).data() + origin,
            (*storage_).row_bytes, bounds, (*storage_).pixel_format,
            active_lock_token_};
}

std::uint64_t Bitmap::commit_edit(std::uint64_t token) {
    require_alive();
    require_edit_token(token);
    const std::size_t edit_row_bytes =
        static_cast<std::size_t>(active_edit_bounds_.width) * 4U;
    std::vector<RectI> damage;
    bool compacted = false;

    for (std::int32_t row = 0; row < active_edit_bounds_.height; ++row) {
        const std::size_t storage_row = pixel_offset(
            *storage_, static_cast<std::uint32_t>(active_edit_bounds_.x),
            static_cast<std::uint32_t>(active_edit_bounds_.y + row));
        const std::byte* current = (*storage_).data() + storage_row;
        const std::byte* original = active_edit_backup_.data() +
            static_cast<std::size_t>(row) * edit_row_bytes;
        std::optional<std::int32_t> run_start;
        for (std::int32_t column = 0; column < active_edit_bounds_.width;
             ++column) {
            const std::size_t pixel = static_cast<std::size_t>(column) * 4U;
            const bool changed = !std::equal(current + pixel,
                                             current + pixel + 4U,
                                             original + pixel);
            if (changed && !run_start) {
                run_start = column;
            } else if (!changed && run_start) {
                const RectI span{active_edit_bounds_.x + *run_start,
                                 active_edit_bounds_.y + row,
                                 column - *run_start, 1};
                if (!damage.empty() && damage.back().x == span.x &&
                    damage.back().width == span.width &&
                    damage.back().bottom() == span.y) {
                    ++damage.back().height;
                } else {
                    damage.push_back(span);
                }
                run_start.reset();
            }
        }
        if (run_start) {
            const RectI span{active_edit_bounds_.x + *run_start,
                             active_edit_bounds_.y + row,
                             active_edit_bounds_.width - *run_start, 1};
            if (!damage.empty() && damage.back().x == span.x &&
                damage.back().width == span.width &&
                damage.back().bottom() == span.y) {
                ++damage.back().height;
            } else {
                damage.push_back(span);
            }
        }
        if (damage.size() > maximum_damage_rectangles) {
            compacted = true;
            break;
        }
    }

    if (compacted) {
        damage.assign(1U, active_edit_bounds_);
    }
    finish_edit();
    if (!damage.empty()) publish_mutation(std::move(damage));
    return generation_;
}

void Bitmap::cancel_edit(std::uint64_t token) {
    require_alive();
    require_edit_token(token);
    const std::size_t edit_row_bytes =
        static_cast<std::size_t>(active_edit_bounds_.width) * 4U;
    for (std::int32_t row = 0; row < active_edit_bounds_.height; ++row) {
        const std::size_t destination = pixel_offset(
            *storage_, static_cast<std::uint32_t>(active_edit_bounds_.x),
            static_cast<std::uint32_t>(active_edit_bounds_.y + row));
        std::copy_n(active_edit_backup_.data() +
                        static_cast<std::size_t>(row) * edit_row_bytes,
                    edit_row_bytes, (*storage_).data() + destination);
    }
    finish_edit();
}

BitmapDamageSnapshot Bitmap::changes_since(std::uint64_t generation) const {
    require_alive();
    require_unlocked();
    if (generation > generation_) {
        throw std::invalid_argument(
            "bitmap damage generation is newer than the bitmap");
    }
    BitmapDamageSnapshot result{generation, generation_, true, {}};
    if (generation == generation_) return result;

    if (damage_history_.empty() ||
        generation < damage_history_.front().generation - 1U) {
        result.history_complete = false;
        result.rectangles.push_back({0, 0,
            static_cast<std::int32_t>((*storage_).width),
            static_cast<std::int32_t>((*storage_).height)});
        return result;
    }

    for (const DamageRecord& record : damage_history_) {
        if (record.generation <= generation) continue;
        if (result.rectangles.size() + record.rectangles.size() >
            maximum_damage_rectangles) {
            result.rectangles.assign(1U, RectI{0, 0,
                static_cast<std::int32_t>((*storage_).width),
                static_cast<std::int32_t>((*storage_).height)});
            return result;
        }
        result.rectangles.insert(result.rectangles.end(),
                                 record.rectangles.begin(),
                                 record.rectangles.end());
    }
    return result;
}

ImageSnapshot Bitmap::snapshot() const {
    require_alive();
    require_unlocked();
    ImageSnapshot result;
    result.stable_id = stable_id_;
    result.width = (*storage_).width;
    result.height = (*storage_).height;
    result.pixel_format = (*storage_).pixel_format;
    result.generation = generation_;
    if ((*storage_).borrowed == nullptr) {
        result.storage_ = storage_;
        return result;
    }
    // Borrowed pixels change under the native object's own drawing, so an
    // immutable snapshot of them is a copy (packed rows, owned).
    std::shared_ptr<PixelStorage> copy = std::make_shared<PixelStorage>();
    (*copy).width = (*storage_).width;
    (*copy).height = (*storage_).height;
    (*copy).pixel_format = (*storage_).pixel_format;
    (*copy).row_bytes = static_cast<std::size_t>((*storage_).width) * 4U;
    (*copy).bytes.resize((*copy).size());
    for (std::uint32_t y = 0; y < (*storage_).height; ++y) {
        std::copy_n((*storage_).data() + static_cast<std::size_t>(y) * (*storage_).row_bytes,
                    (*copy).row_bytes,
                    (*copy).bytes.data() + static_cast<std::size_t>(y) * (*copy).row_bytes);
    }
    result.storage_ = std::move(copy);
    return result;
}

void Bitmap::on_dispose() noexcept {
    active_lock_token_ = 0U;
    active_edit_ = false;
    active_edit_backup_.clear();
    damage_history_.clear();
    storage_.reset();
}

void Bitmap::require_unlocked() const {
    if (active_lock_token_ != 0U) {
        throw std::logic_error("bitmap operation is unavailable during a pixel lock");
    }
}

void Bitmap::require_coordinate(std::uint32_t x, std::uint32_t y) const {
    if (x >= (*storage_).width || y >= (*storage_).height) {
        throw std::out_of_range("bitmap pixel coordinate is outside the image");
    }
}

void Bitmap::require_edit_token(std::uint64_t token) const {
    if (!active_edit_ || active_lock_token_ == 0U || token == 0U ||
        token != active_lock_token_) {
        throw std::invalid_argument(
            "bitmap edit token is invalid or already released");
    }
}

void Bitmap::prepare_write() {
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("bitmap generation space exhausted");
    }
    // Copy on write for owned pixels a snapshot shares. Borrowed pixels are
    // never shared (snapshot copies them), so they are written in place.
    if (storage_.use_count() != 1 && (*storage_).borrowed == nullptr) {
        storage_ = std::make_shared<PixelStorage>(*storage_);
    }
}

void Bitmap::publish_mutation(std::vector<RectI> damage) {
    if (damage.empty()) return;
    if (damage_history_.size() == maximum_damage_history) {
        damage_history_.erase(damage_history_.begin());
    }
    const std::uint64_t next_generation = generation_ + 1U;
    damage_history_.push_back({next_generation, std::move(damage)});
    generation_ = next_generation;
}

void Bitmap::finish_edit() noexcept {
    active_lock_token_ = 0U;
    lock_mode_ = BitmapLockMode::read;
    active_edit_ = false;
    active_edit_bounds_ = {};
    active_edit_backup_.clear();
}


} // namespace gui_drawing
