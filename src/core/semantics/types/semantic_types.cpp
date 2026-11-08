#include "gui_forms/semantics/types/semantic_types.hpp"

namespace gui_forms {

namespace {

// An FNV-1a hash with the 64-bit FNV prime and the offset basis every former
// copy of this helper used: 1469598103934665603, the published basis
// (14695981039346656037) with its last digit dropped. It is kept so every
// runtime ID stays what it was; the hash only needs to spread stable IDs.
constexpr std::uint64_t offset_basis = 1469598103934665603ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
// The top bit, which a control's own runtime ID (counted up from 1) never
// has.
constexpr std::uint64_t virtual_id_bit = 1ULL << 63U;

} // namespace

std::uint64_t virtual_semantic_runtime_id(std::string_view stable_id) noexcept {
    std::uint64_t hash = offset_basis;
    for (const unsigned char byte : stable_id) {
        hash ^= byte;
        hash *= fnv_prime;
    }
    const std::uint64_t result = hash | virtual_id_bit;
    return result;
}

bool is_virtual_semantic_runtime_id(std::uint64_t runtime_id) noexcept {
    const bool virtual_id = (runtime_id & virtual_id_bit) != 0U;
    return virtual_id;
}

bool semantic_announcement_due(const SemanticNode* previous, const SemanticNode& current) noexcept {
    if (current.role != SemanticRole::live_region || current.name.empty()) return false;
    if (previous == nullptr) return current.announces_on_appearance;
    const bool renamed = (*previous).name != current.name;
    return renamed;
}

} // namespace gui_forms
