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
//
// A raiser with a parameter (on_key_down(KeyEventArgs&), on_mouse_down(const
// MouseEventArgs&)) lends the entry its argument as the event's fields (the
// event catalogue's field forms, abi_event_fields.hpp), writable for a
// mutable reference. Base reads what the entry wrote into the argument,
// runs the raiser below with it, and publishes the argument again as the
// subscribers left it; when the entry returns, what it wrote after base is
// read into the argument, which the window then sees.

#pragma once

#include "gui_forms/c/gf_abi.h"

#include "gui_forms/control.hpp"

#include "../events/abi_event_fields.hpp"
#include "../failure/abi_failure.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
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

// The C++ argument of the raise an entry runs in, for base: a raiser with a
// mutable reference parameter lends value, one with a const reference or a
// value parameter constant_value; a raiser without parameters lends
// neither. Both are borrowed for the raise.
struct HookArguments final {
    void* value = nullptr;
    const void* constant_value = nullptr;
};

// How a raise's argument becomes event fields and back (one pair per
// parameter type, HookFieldForms): publish appends the argument's fields,
// absorb reads the fields an entry wrote into the argument. Null for a
// raiser whose argument has no field form (the event has no fields).
struct HookFieldOperations final {
    void (*publish)(EventFields& fields, const HookArguments& arguments) = nullptr;
    void (*absorb)(EventFields& fields, const HookArguments& arguments) = nullptr;
};

// What running the raiser below a layer came to.
enum class HookRun : std::uint8_t {
    ran,
    // No layer of the bridge has the hook's segment, or the segment no such
    // entry.
    no_segment,
    // The raiser takes an argument that only a raise in progress can lend
    // (on_paint_background's painter), and base was called outside one.
    needs_arguments,
};

// The entry a thread is running, innermost last: a base function called with
// the frame's event finds its bridge here, without the registry (the object
// may have no identity yet). Frames live on the C++ stack of enter_hook and
// link through previous; the top is thread-local.
struct HookFrame final {
    ClassBridge* bridge = nullptr;
    HookKey hook{};
    const gf_event* event = nullptr;
    HookFrame* previous = nullptr;
    // The raise's argument, and the fields it lends the entry (null for a
    // raiser whose argument has none). event is published_event, which base
    // publishes again after the raiser below ran.
    HookArguments arguments{};
    EventFields* fields = nullptr;
    gf_event* published_event = nullptr;
    HookFieldOperations operations{};
    std::string_view name;
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

    // Takes the validated table and the caller's instance. Before arm_class_table.
    void bind_class_table(const ClassTableView& view, void* instance) noexcept;
    // From here the destructor calls release. Arming last means a bridge
    // whose construction throws calls no release (create refused it).
    void arm_class_table() noexcept { armed_ = true; }

    // Runs the raiser below the table for hook with arguments (empty outside
    // an entry: a raiser with a parameter then makes a default argument, or
    // reports needs_arguments when it cannot).
    [[nodiscard]] virtual HookRun run_class_layer_base(HookKey hook, const HookArguments& arguments) = 0;
    // The control this bridge is part of.
    [[nodiscard]] virtual Control& bridged_control() noexcept = 0;

    // The innermost frame of this thread whose event is event; null when
    // none.
    [[nodiscard]] static HookFrame* find_hook_frame(const gf_event* event) noexcept;

protected:
    // The caller's entry for hook; null before arm_class_table.
    [[nodiscard]] gf_hook_entry class_hook_entry(HookKey hook) const noexcept;
    // Calls entry for control with a property event named property and base,
    // inside a frame, keeping control alive through the call. False, and
    // nothing called, when control is no longer alive: the layer then runs
    // the raiser below as if there were no table. A C++ exception base caught
    // is rethrown after entry returns.
    [[nodiscard]] bool enter_class_hook(Control& control, gf_hook_entry entry, HookKey hook,
                                        std::string_view property, gf_base_raiser base);
    // The same for a raiser with one parameter: argument is lent to base,
    // and, when Fields, to the entry as the event's fields (writable when
    // Parameter is not const). After the entry returns, the fields it wrote
    // are read back into argument.
    template <bool Fields, typename Parameter>
    [[nodiscard]] bool enter_class_hook_with(Control& control, gf_hook_entry entry, HookKey hook,
                                             std::string_view name, gf_base_raiser base, Parameter& argument);

private:
    // enter_class_hook and enter_class_hook_with: the frame, the event and
    // the call.
    [[nodiscard]] bool enter_class_frame(Control& control, gf_hook_entry entry, HookKey hook, std::string_view name,
                                         gf_base_raiser base, const HookArguments& arguments,
                                         const HookFieldOperations& operations);

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

    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        static_cast<void>(hook);
        static_cast<void>(arguments);
        return HookRun::no_segment;
    }
};

// The field forms of one parameter type (HookFieldOperations): Parameter is
// const for a const reference or a value, and only a mutable one reads the
// entry's writes back.
template <typename Parameter>
struct HookFieldForms final {
    using Value = std::remove_const_t<Parameter>;
    static constexpr bool writable = !std::is_const_v<Parameter>;

    static const Value& read_argument_of(const HookArguments& arguments) noexcept {
        const void* address = writable ? static_cast<const void*>(arguments.value) : arguments.constant_value;
        const Value& value = *static_cast<const Value*>(address);
        return value;
    }

    static void publish(EventFields& fields, const HookArguments& arguments) {
        const Value& value = read_argument_of(arguments);
        append_argument<writable>(fields, 0U, 1U, value);
    }

    static void absorb(EventFields& fields, const HookArguments& arguments) {
        if constexpr (writable) {
            Value& value = *static_cast<Value*>(arguments.value);
            read_argument<true>(fields, 0U, 1U, value);
        } else {
            static_cast<void>(fields);
            static_cast<void>(arguments);
        }
    }
};

template <bool Fields, typename Parameter>
bool ClassBridge::enter_class_hook_with(Control& control, gf_hook_entry entry, HookKey hook, std::string_view name,
                                        gf_base_raiser base, Parameter& argument) {
    HookArguments arguments{};
    if constexpr (std::is_const_v<Parameter>) {
        arguments.constant_value = &argument;
    } else {
        arguments.value = &argument;
    }
    HookFieldOperations operations{};
    if constexpr (Fields) {
        operations.publish = &HookFieldForms<Parameter>::publish;
        operations.absorb = &HookFieldForms<Parameter>::absorb;
    }
    const bool entered = enter_class_frame(control, entry, hook, name, base, arguments, operations);
    return entered;
}

// The complete bridge over Layers (the outermost segment layer).
template <typename Layers>
class FinalBridge final : public Layers {
public:
    using Layers::Layers;

    [[nodiscard]] HookRun run_class_layer_base(HookKey hook, const HookArguments& arguments) override {
        const HookRun run = Layers::run_layer_base(hook, arguments);
        return run;
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
        (*made).bind_class_table(view, instance);
        (*made).arm_class_table();
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
// GF_STATUS_WRONG_HANDLE_KIND for an object whose chain lacks hook's segment,
// GF_STATUS_INVALID_ARGUMENT for a raiser whose argument only a raise in
// progress can lend, called outside one.
[[nodiscard]] gf_status run_class_base(gf_object self, gf_event* event, HookKey hook) noexcept;

} // namespace gui_forms::abi1
