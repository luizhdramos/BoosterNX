#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>

namespace opennow
{

void SetCachedCoverImage(brls::Image* image, const std::string& image_url);
void SetCachedThumbnailImage(brls::Image* image, const std::string& image_url);
void SetCachedGalleryImage(
    brls::Image* image,
    const std::string& image_url,
    std::function<bool()> should_apply = {});
void PrefetchCachedGalleryImage(const std::string& image_url);
void SetCachedAvatarImage(brls::Image* image, const std::string& image_url);
void ShutdownImageCache();

} // namespace opennow
