#include "command_directory.hpp"

#include <cstddef>
#include <mutex>

namespace gui_forms::detail {
namespace {

struct CommandDirectory final {
    std::mutex mutex{};
    std::vector<Command*> commands{};
    std::uint64_t generation{1U};
};

CommandDirectory& directory() noexcept {
    // Never destroyed (termination releases nothing): a late call at exit
    // never meets a destroyed lock.
    static CommandDirectory* const instance = new CommandDirectory();
    return *instance;
}

} // namespace

void note_command(Command& command) {
    CommandDirectory& table = directory();
    const std::lock_guard<std::mutex> lock(table.mutex);
    table.commands.push_back(&command);
    ++table.generation;
}

void forget_command(const Command& command) noexcept {
    CommandDirectory& table = directory();
    const std::lock_guard<std::mutex> lock(table.mutex);
    for (std::size_t index = 0U; index < table.commands.size(); ++index) {
        if (table.commands[index] != &command) continue;
        // Order is kept: the tool list follows the order commands were made.
        table.commands.erase(table.commands.begin() + static_cast<std::ptrdiff_t>(index));
        ++table.generation;
        return;
    }
}

void fill_live_commands(std::vector<Command*>& commands) {
    CommandDirectory& table = directory();
    const std::lock_guard<std::mutex> lock(table.mutex);
    commands = table.commands;
}

std::uint64_t command_directory_generation() noexcept {
    CommandDirectory& table = directory();
    const std::lock_guard<std::mutex> lock(table.mutex);
    const std::uint64_t generation = table.generation;
    return generation;
}

} // namespace gui_forms::detail
