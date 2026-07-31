#include "image_url_policy.hpp"

namespace opennow
{

std::string ResizeNvidiaImageUrl(std::string image_url, int width)
{
    if (width <= 0 ||
        image_url.find("img.nvidiagrid.net") == std::string::npos)
    {
        return image_url;
    }

    const auto marker = image_url.find(";w=");
    if (marker != std::string::npos)
    {
        auto end = marker + 3;
        while (end < image_url.size() &&
               image_url[end] >= '0' &&
               image_url[end] <= '9')
        {
            ++end;
        }
        image_url.replace(
            marker + 3, end - (marker + 3), std::to_string(width));
        return image_url;
    }
    return image_url + ";w=" + std::to_string(width);
}

} // namespace opennow
