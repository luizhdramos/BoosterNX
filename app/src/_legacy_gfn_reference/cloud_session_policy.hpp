#pragma once

#include <string_view>

namespace opennow::cloud_session
{

constexpr bool IsActiveStatus(int status)
{
    return status == 1 || status == 2 || status == 3;
}

constexpr bool IsDeviceLimitError(int status_code, std::string_view description)
{
    return status_code == 50 ||
        description.find("SESSION_LIMIT_PER_DEVICE_EXCEEDED") != std::string_view::npos;
}

constexpr bool ShouldCleanup(
    int status,
    std::string_view session_id,
    std::string_view session_device_id,
    std::string_view current_device_id,
    std::string_view locally_tracked_session_id)
{
    if (!IsActiveStatus(status) || session_id.empty())
        return false;

    if (!locally_tracked_session_id.empty() && session_id == locally_tracked_session_id)
        return true;

    return !session_device_id.empty() &&
        !current_device_id.empty() &&
        session_device_id == current_device_id;
}

} // namespace opennow::cloud_session
