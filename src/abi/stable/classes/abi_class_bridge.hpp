// The bridge between a caller's class table (gf_abi_classes.h) and the C++
// raisers it overrides. tools/abi_class_tables.py generates one layer per
// segment (generated/abi_class_segments.hpp); each overrides its class's
// raisers and calls the table's entry where there is one. A bridge is
//
//     FinalBridge<SegmentLayer<...SegmentLayer<BridgeState<Stock>>>>
//
// with the chain's segments base first, so the most derived segment's layer
// is outermost. The order contract is gf_abi_classes.h's: the entry runs
// where the raiser runs, and base runs the raiser below the layer (the stock
// class's own override, then its bases, ending in the emission).

#pragma once

#include "gui_forms/c/gf_abi.h"

#include "gui_forms/control.hpp"

#include "../failure/abi_failure.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gui_forms::abi1 {

// One raiser: its class's segment and its entry's position there.
struct HookKey final {
    gf_class_id class_id = 0U;
    std::uint32_t entry = 0U;
};

// The most segments one class's chain has. The registry checks every chain
// against it.
inline constexpr std::size_t max_class_segments = 16U;

// The bytes of a gf_segment_header, and of one entry.
inline constexpr std::uint32_t segment_header_bytes = 8U;
inline constexpr std::uint32_t hook_entry_bytes = static_cast<std::uint32_t>(sizeof(gf_hook_entry));
// The 1.3 size of gf_class_header; a caller's may be larger (a later minor).
inline constexpr std::uint32_t class_header_size_1_3 = 24U;
static_assert(sizeof(gf_class_header) == class_header_size_1_3);
static_assert(sizeof(gf_segment_header) == segment_header_bytes);

// gf_class_header's release.
using ClassRelease = void(GF_CALL*)(void* instance);

// A caller's class table as create validated it: the segment pointers it
// found, each with its class and size. The table itself stays the caller's
// (borrowed for the objects' lives, gf_classes_table.create); the view
// copies the pointers, never the entries, which are read at each raise.
class ClassTableView final {
public:
    // One segment the table supplied: null bytes when the pointer was null.
    struct Segment final {
        gf_class_id class_id = 0U;
        std::uint32_t struct_size = 0U;
        const unsigned char* bytes = nullptr;
    };

    // Validates table against chain (the class's segment ids, base first)
    // and fills view. On failure records it for slot and leaves view empty.
    [[nodiscard]] static gf_status read(const gf_class_header* table, std::span<const gf_class_id> chain,
                                        SlotIdentity slot, ClassTableView& view) noexcept;

    // The caller's entry for hook, null when the table has no such segment,
    // the segment is shorter than the entry, or the entry is null.
    [[nodiscard]] gf_hook_entry entry(HookKey hook) const noexcept;
    // The header's release function, null when the caller gave none.
    [[nodiscard]] ClassRelease release() const noexcept { return release_; }

private:
    std::array<Segment, max_class_segments> segments_{};
    // The live length of segments_.
    std::size_t segment_count_ = 0U;
    ClassRelease release_ = nullptr;
};

class ClassBridge;

// The entry a thread is running, innermost last: a base function called with
// the frame's event finds its bridge here, without the registry (the object
// may have no identity yet). Frames live on the C++ stack of enter_hook and
// link through previous; the top is thread-local.
struct HookFrame final {
    ClassBridge* bridge = nullptr;
    HookKey hook{};
    const gf_event* event = nullptr;
    HookFrame* previous = nullptr;
    // A C++ exception base caught from the raiser below, rethrown once the
    // entry returns so the raise fails as it would without a table.
    std::exception_ptr failure;
};

// The table-dependent half of every bridge: the caller's view and instance,
// the frames, and the release. One per object, on the UI thread; never
// copied or moved (the object owns it as a base).
class ClassBridge {
public:
    ClassBridge() = default;
    ClassBridge(const ClassBridge&) = delete;
    ClassBridge& operator=(const ClassBridge&) = delete;
    ClassBridge(ClassBridge&&) = delete;
    ClassBridge& operator=(ClassBridge&&) = delete;
    // Calls release(instance) once when armed.
    virtual ~ClassBridge();

    // Takes the validated table and the caller's instance. Before arm.
    void bind(const ClassTableView& view, void* instance) noexcept;
    // From here the destructor calls release. Arming last means a bridge
    // whose construction throws calls no release (create refused it).
    void arm() noexcept { armed_ = true; }

    // Runs the raiser below the table for hook: false when no layer of this
    // bridge has hook's segment.
    [[nodiscard]] virtual bool run_base(HookKey hook) = 0;
    // The control this bridge is part of.
    [[nodiscard]] virtual Control& bridged_control() noexcept = 0;

    // The innermost frame of this thread whose event is event; null when
    // none.
    [[nodiscard]] static HookFrame* find_frame(const gf_event* event) noexcept;

protected:
    // The caller's entry for hook; null before arm.
    [[nodiscard]] gf_hook_entry hook_entry(HookKey hook) const noexcept;
    // Calls entry for control with a property event named property and base,
    // inside a frame, keeping control alive through the call. False, and
    // nothing called, when control is no longer alive: the layer then runs
    // the raiser below as if there were no table. A C++ exception base caught
    // is rethrown after entry returns.
    [[nodiscard]] bool enter_hook(Control& control, gf_hook_entry entry, HookKey hook, std::string_view property,
                                  gf_base_raiser base);

private:
    ClassTableView table_{};
    void* instance_ = nullptr;
    bool armed_ = false;
};

// The innermost layer: the stock class and the table's state. A layer's
// run_layer_base ends here when no layer had the hook.
template <typename Stock>
class BridgeState : public Stock, public ClassBridge {
public:
    using StockType = Stock;
    using Stock::Stock;

    [[nodiscard]] bool run_layer_base(HookKey hook) {
        static_cast<void>(hook);
        return false;
    }
};

// The complete bridge over Layers (the outermost segment layer).
template <typename Layers>
class FinalBridge final : public Layers {
public:
    using Layers::Layers;

    [[nodiscard]] bool run_base(HookKey hook) override {
        const bool ran = Layers::run_layer_base(hook);
        return ran;
    }

    [[nodiscard]] Control& bridged_control() noexcept override { return *this; }
};

// Makes a bridge over Layers from stable_id, bound to view and instance and
// armed; null when the stock class is not made from a stable id alone (an
// abstract base, a class whose constructor needs more), so classes.create
// refuses with GF_STATUS_UNSUPPORTED_CAPABILITY.
using BridgeFactory = Control::Ptr (*)(StableId stable_id, const ClassTableView& view, void* instance);

template <typename Layers>
Control::Ptr create_bridge(StableId stable_id, const ClassTableView& view, void* instance) {
    using Stock = typename Layers::StockType;
    if constexpr (std::is_constructible_v<Stock, StableId> && !std::is_abstract_v<Stock>) {
        std::shared_ptr<FinalBridge<Layers>> made = make_control<FinalBridge<Layers>>(std::move(stable_id));
        (*made).bind(view, instance);
        (*made).arm();
        Control::Ptr control = std::move(made);
        return control;
    } else {
        static_cast<void>(stable_id);
        static_cast<void>(view);
        static_cast<void>(instance);
        return nullptr;
    }
}

// One class classes.create makes (generated/abi_class_registry.cpp).
struct ClassDescription final {
    gf_class_id class_id = 0U;
    // The schema type the bridge derives from, for messages.
    std::string_view type_id;
    // The chain's segment ids, base first: the order of the table's pointers.
    std::span<const gf_class_id> segments;
    BridgeFactory create = nullptr;
};

// Every class with a class table, in ledger order (generated).
[[nodiscard]] std::span<const ClassDescription> class_descriptions() noexcept;
// The base function of class_id's entry, null when there is none (generated).
[[nodiscard]] gf_base_raiser find_base_raiser(gf_class_id class_id, std::uint32_t entry) noexcept;

// What every base function does: in an entry's frame (event is the frame's),
// runs the bridge's raiser below the table; otherwise finds self through the
// registry on the UI thread. GF_STATUS_STALE_HANDLE once the object is
// disposed, GF_STATUS_WRONG_THREAD off the UI thread,
// GF_STATUS_WRONG_HANDLE_KIND for an object whose chain lacks hook's segment.
[[nodiscard]] gf_status run_class_base(gf_object self, gf_event* event, HookKey hook) noexcept;

} // namespace gui_forms::abi1
