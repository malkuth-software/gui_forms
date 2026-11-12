#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/image_attributes/image_attributes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gui_drawing {

enum class BitmapLockMode : std::uint8_t { read, write, read_write };

struct GUI_FORMS_API BitmapLockView final {
    const std::byte* data{};
    std::byte* writable_data{};
    std::size_t row_bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
    PixelFormat pixel_format{PixelFormat::bgra32_premultiplied};
    std::uint64_t token{};
};

/*
 * A bounded writable lease over one bitmap rectangle.  The pointer addresses
 * the rectangle's top-left pixel while row_bytes remains the bitmap stride.
 * Callers must commit or cancel exactly once.  Cancel restores the leased
 * rectangle byte-for-byte; commit derives conservative bounded damage from
 * the pixels whose bytes actually changed.
 */
struct GUI_FORMS_API BitmapEditView final {
    const std::byte* data{};
    std::byte* writable_data{};
    std::size_t row_bytes{};
    RectI bounds{};
    PixelFormat pixel_format{PixelFormat::bgra32_premultiplied};
    std::uint64_t token{};
};

struct GUI_FORMS_API BitmapDamageSnapshot final {
    std::uint64_t from_generation{};
    std::uint64_t to_generation{};
    bool history_complete{true};
    std::vector<RectI> rectangles;

    [[nodiscard]] bool empty() const noexcept { return rectangles.empty(); }
};

class GUI_FORMS_API Bitmap final : public DrawingObject {
public:
    static constexpr std::uint32_t maximum_dimension = 32768;
    static constexpr std::uint64_t maximum_pixels = 100'000'000ULL;
    static constexpr std::uint64_t maximum_bytes = maximum_pixels * 4ULL;
    static constexpr std::size_t maximum_damage_rectangles = 256;
    static constexpr std::size_t maximum_damage_history = 256;

    Bitmap(std::uint32_t width, std::uint32_t height,
           PixelFormat pixel_format = PixelFormat::bgra32_premultiplied);

    // A bitmap over memory it does not own: width x height premultiplied
    // BGRA rows, row_bytes apart, at memory (a native DIB section's pixels).
    // The caller keeps the memory alive and unmoved for the bitmap's life.
    // Drawing writes those pixels in place; a snapshot copies them.
    [[nodiscard]] static std::unique_ptr<Bitmap> borrow(std::uint32_t width, std::uint32_t height,
                                                        std::size_t row_bytes, std::byte* memory);
    [[nodiscard]] bool borrows_memory() const;
    // The borrowed memory, or nullptr for a bitmap that owns its pixels.
    [[nodiscard]] const std::byte* borrowed_memory() const;

    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] std::uint32_t height() const;
    [[nodiscard]] PixelFormat pixel_format() const;
    [[nodiscard]] std::uint64_t generation() const;
    [[nodiscard]] Color get_pixel(std::uint32_t x, std::uint32_t y) const;
    void set_pixel(std::uint32_t x, std::uint32_t y, Color color);
    void make_transparent(Color key);
    [[nodiscard]] std::unique_ptr<Bitmap> clone(RectI source) const;
    [[nodiscard]] std::unique_ptr<Bitmap> thumbnail(std::uint32_t width,
                                                    std::uint32_t height) const;
    [[nodiscard]] std::unique_ptr<Bitmap> adjusted(
        const ImageAttributes& attributes) const;

    [[nodiscard]] BitmapLockView lock(BitmapLockMode mode);
    void unlock(std::uint64_t token);
    [[nodiscard]] bool locked() const;
    [[nodiscard]] BitmapEditView begin_edit(RectI bounds);
    [[nodiscard]] std::uint64_t commit_edit(std::uint64_t token);
    void cancel_edit(std::uint64_t token);
    [[nodiscard]] BitmapDamageSnapshot changes_since(
        std::uint64_t generation) const;
    [[nodiscard]] ImageSnapshot snapshot() const;

protected:
    void on_dispose() noexcept override;

private:
    void require_unlocked() const;
    void require_coordinate(std::uint32_t x, std::uint32_t y) const;
    void require_edit_token(std::uint64_t token) const;
    void prepare_write();
    void publish_mutation(std::vector<RectI> damage);
    void finish_edit() noexcept;

    struct DamageRecord final {
        std::uint64_t generation{};
        std::vector<RectI> rectangles;
    };

    std::shared_ptr<PixelStorage> storage_;
    std::uint64_t stable_id_{};
    std::uint64_t generation_{1};
    std::uint64_t next_lock_token_{1};
    std::uint64_t active_lock_token_{};
    BitmapLockMode lock_mode_{BitmapLockMode::read};
    bool active_edit_{};
    RectI active_edit_bounds_{};
    std::vector<std::byte> active_edit_backup_;
    std::vector<DamageRecord> damage_history_;
};

} // namespace gui_drawing
