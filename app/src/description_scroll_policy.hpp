#pragma once

#include <algorithm>

namespace opennow
{

enum class DescriptionScrollDirection
{
    Up,
    Down,
};

struct DescriptionScrollResult
{
    float offset = 0.0f;
    bool return_to_buttons = false;
};

inline DescriptionScrollResult AdvanceDescriptionScroll(
    float current_offset,
    float maximum_offset,
    DescriptionScrollDirection direction,
    float step)
{
    constexpr float kBoundaryEpsilon = 0.5f;
    current_offset = std::clamp(
        current_offset, 0.0f, std::max(0.0f, maximum_offset));
    step = std::max(1.0f, step);

    if (direction == DescriptionScrollDirection::Up)
    {
        if (current_offset <= kBoundaryEpsilon)
            return {0.0f, true};
        return {std::max(0.0f, current_offset - step), false};
    }

    return {
        std::min(std::max(0.0f, maximum_offset), current_offset + step),
        false,
    };
}

} // namespace opennow
