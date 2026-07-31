#pragma once

#include "models.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// Simplified relative to SwitchNOW/GFN's library_sort.hpp: no "Store" sort
// mode (Boosteroid's GameInfo has no publisher/available_stores — see
// models.hpp) and "Last Added" just keeps the server's own feed order,
// matching the original's behavior for that mode.
namespace opennow
{

enum class LibrarySortMode
{
    LastPlayed = 0,
    LastAdded = 1,
    Title = 2,
};

inline std::string LibrarySortKey(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline void SortLibraryIndices(
    std::vector<size_t>& indices,
    const std::vector<GameInfo>& games,
    LibrarySortMode mode)
{
    if (mode == LibrarySortMode::LastAdded)
        return; // Keep the server's own feed order untouched.

    std::stable_sort(indices.begin(), indices.end(), [&](size_t left_index, size_t right_index) {
        const GameInfo& left = games[left_index];
        const GameInfo& right = games[right_index];

        if (mode == LibrarySortMode::LastPlayed)
        {
            if (left.last_played.empty() != right.last_played.empty())
                return !left.last_played.empty();
            if (left.last_played != right.last_played)
                return left.last_played > right.last_played;
        }

        const std::string left_title = LibrarySortKey(left.title);
        const std::string right_title = LibrarySortKey(right.title);
        if (left_title != right_title)
            return left_title < right_title;
        return left.id < right.id;
    });
}

} // namespace opennow
