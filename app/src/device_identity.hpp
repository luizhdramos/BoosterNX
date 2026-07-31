#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace opennow
{

inline std::string NormalizeCloudDeviceId(std::string_view stored_id)
{
    std::string hex;
    hex.reserve(32);
    for (const char value : stored_id)
    {
        if (value == '-')
            continue;

        const unsigned char byte = static_cast<unsigned char>(value);
        if (!std::isxdigit(byte))
            return {};

        if (hex.size() < 32)
            hex.push_back(static_cast<char>(std::tolower(byte)));
    }

    if (hex.size() != 32)
        return {};

    // Keep the ID deterministic while presenting the UUID shape used by the
    // official desktop client.
    hex[12] = '4';
    const int variant = std::isdigit(static_cast<unsigned char>(hex[16]))
        ? hex[16] - '0'
        : hex[16] - 'a' + 10;
    hex[16] = "89ab"[variant & 0x3];

    return hex.substr(0, 8) + "-" +
        hex.substr(8, 4) + "-" +
        hex.substr(12, 4) + "-" +
        hex.substr(16, 4) + "-" +
        hex.substr(20, 12);
}

} // namespace opennow
