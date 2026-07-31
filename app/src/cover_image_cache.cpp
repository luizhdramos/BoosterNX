#include "cover_image_cache.hpp"

#include "http_client.hpp"

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace opennow
{
namespace
{

constexpr const char* kFallbackCoverRes = "img/opennow_switch_icon.jpg";
constexpr const char* kImageUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";
constexpr size_t kMemoryCacheLimit = 16 * 1024 * 1024;
constexpr size_t kMaximumCachedImageSize = 6 * 1024 * 1024;
constexpr int kImageWorkerCount = 3;
constexpr int kThumbnailWidth = 420;
constexpr int kGalleryWidth = 1120;

enum class ImagePriority : int
{
    Prefetch = 0,
    Normal = 1,
    Critical = 2,
};

using ImageData = std::shared_ptr<const std::string>;
using Completion = std::function<void(ImageData)>;

void EnsureImageCacheDirectory()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/BoosterNX", 0777);
    mkdir("sdmc:/switch/BoosterNX/cache", 0777);
    mkdir("sdmc:/switch/BoosterNX/cache/images", 0777);
#endif
}

std::string HashUrl(const std::string& url)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : url)
    {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

std::string CachePathForUrl(const std::string& url)
{
    return "sdmc:/switch/BoosterNX/cache/images/" + HashUrl(url) + ".img";
}

bool HasImageSignature(const std::string& data)
{
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xff &&
        static_cast<unsigned char>(data[1]) == 0xd8 &&
        static_cast<unsigned char>(data[2]) == 0xff)
    {
        return true;
    }

    static constexpr unsigned char kPngSignature[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };
    if (data.size() >= sizeof(kPngSignature) &&
        std::equal(
            std::begin(kPngSignature), std::end(kPngSignature),
            reinterpret_cast<const unsigned char*>(data.data())))
    {
        return true;
    }

    if (data.size() >= 6 &&
        (data.compare(0, 6, "GIF87a") == 0 ||
         data.compare(0, 6, "GIF89a") == 0))
    {
        return true;
    }

    return data.size() >= 12 &&
           data.compare(0, 4, "RIFF") == 0 &&
           data.compare(8, 4, "WEBP") == 0;
}

bool ReadCachedImage(const std::string& path, std::string& data)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
        return false;

    const std::streampos size = stream.tellg();
    if (size <= 0 ||
        static_cast<std::uint64_t>(size) > kMaximumCachedImageSize)
    {
        stream.close();
        std::remove(path.c_str());
        return false;
    }

    stream.seekg(0, std::ios::beg);
    data.assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    if (!data.empty() && HasImageSignature(data))
        return true;

    stream.close();
    std::remove(path.c_str());
    data.clear();
    return false;
}

void WriteCachedImage(const std::string& path, const std::string& data)
{
    if (data.empty() || !HasImageSignature(data))
        return;

    EnsureImageCacheDirectory();
    const std::string temporary_path = path + ".tmp";
    {
        std::ofstream stream(
            temporary_path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
            return;

        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream.good())
        {
            stream.close();
            std::remove(temporary_path.c_str());
            return;
        }
    }

    std::remove(path.c_str());
    if (std::rename(temporary_path.c_str(), path.c_str()) != 0)
        std::remove(temporary_path.c_str());
}

class ImageLoader
{
  public:
    ImageLoader()
    {
        workers_.reserve(kImageWorkerCount);
        for (int index = 0; index < kImageWorkerCount; ++index)
            workers_.emplace_back([this]() { WorkerLoop(); });
    }

    ~ImageLoader()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            critical_queue_.clear();
            normal_queue_.clear();
            prefetch_queue_.clear();
        }
        condition_.notify_all();
        for (auto& worker : workers_)
        {
            if (worker.joinable())
                worker.join();
        }
    }

    void Request(
        const std::string& url,
        ImagePriority priority,
        Completion completion = {})
    {
        ImageData memory_hit;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto cached = memory_cache_.find(url);
            if (cached != memory_cache_.end())
            {
                cached->second.last_used = ++access_counter_;
                memory_hit = cached->second.data;
            }
            else
            {
                auto pending = pending_.find(url);
                if (pending != pending_.end())
                {
                    if (completion)
                        pending->second->completions.push_back(
                            std::move(completion));
                    if (!pending->second->started &&
                        static_cast<int>(priority) >
                            static_cast<int>(pending->second->priority))
                    {
                        pending->second->priority = priority;
                        Queue(url, priority);
                        condition_.notify_one();
                    }
                    return;
                }

                auto state = std::make_shared<PendingRequest>();
                state->priority = priority;
                if (completion)
                    state->completions.push_back(std::move(completion));
                pending_.emplace(url, std::move(state));
                Queue(url, priority);
                condition_.notify_one();
                return;
            }
        }

        if (completion)
            completion(std::move(memory_hit));
    }

  private:
    struct MemoryEntry
    {
        ImageData data;
        std::uint64_t last_used = 0;
    };

    struct PendingRequest
    {
        ImagePriority priority = ImagePriority::Normal;
        bool started = false;
        std::vector<Completion> completions;
    };

    struct QueueEntry
    {
        std::string url;
        ImagePriority priority = ImagePriority::Normal;
    };

    void Queue(const std::string& url, ImagePriority priority)
    {
        QueueEntry entry {url, priority};
        if (priority == ImagePriority::Critical)
            critical_queue_.push_back(std::move(entry));
        else if (priority == ImagePriority::Normal)
            normal_queue_.push_back(std::move(entry));
        else
            prefetch_queue_.push_back(std::move(entry));
    }

    bool PopNext(QueueEntry& entry)
    {
        auto pop = [&entry](std::deque<QueueEntry>& queue) {
            if (queue.empty())
                return false;
            entry = std::move(queue.front());
            queue.pop_front();
            return true;
        };
        return pop(critical_queue_) ||
               pop(normal_queue_) ||
               pop(prefetch_queue_);
    }

    ImageData Load(const std::string& url)
    {
        try
        {
            const std::string cache_path = CachePathForUrl(url);
            std::string bytes;
            if (ReadCachedImage(cache_path, bytes))
                return std::make_shared<const std::string>(std::move(bytes));

            HttpClient http_client;
            const HttpResponse response = http_client.Get(
                url,
                kImageUserAgent,
                {"Accept: image/jpeg,image/png,image/*,*/*;q=0.8"},
                5,
                15);
            if (response.status_code != 200 ||
                response.body.empty() ||
                response.body.size() > kMaximumCachedImageSize ||
                !HasImageSignature(response.body))
            {
                return {};
            }

            WriteCachedImage(cache_path, response.body);
            return std::make_shared<const std::string>(response.body);
        }
        catch (...)
        {
            return {};
        }
    }

    void InsertMemoryCache(const std::string& url, const ImageData& data)
    {
        if (!data || data->empty() || data->size() > kMaximumCachedImageSize)
            return;

        const auto existing = memory_cache_.find(url);
        if (existing != memory_cache_.end())
            memory_cache_bytes_ -= existing->second.data->size();

        MemoryEntry entry;
        entry.data = data;
        entry.last_used = ++access_counter_;
        memory_cache_bytes_ += data->size();
        memory_cache_[url] = std::move(entry);

        while (memory_cache_bytes_ > kMemoryCacheLimit &&
               memory_cache_.size() > 1)
        {
            auto oldest = std::min_element(
                memory_cache_.begin(), memory_cache_.end(),
                [](const auto& left, const auto& right) {
                    return left.second.last_used < right.second.last_used;
                });
            if (oldest == memory_cache_.end())
                break;
            memory_cache_bytes_ -= oldest->second.data->size();
            memory_cache_.erase(oldest);
        }
    }

    void WorkerLoop()
    {
        while (true)
        {
            QueueEntry entry;
            std::shared_ptr<PendingRequest> state;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() {
                    return stopping_ ||
                           !critical_queue_.empty() ||
                           !normal_queue_.empty() ||
                           !prefetch_queue_.empty();
                });
                if (stopping_)
                    return;
                if (!PopNext(entry))
                    continue;

                const auto pending = pending_.find(entry.url);
                if (pending == pending_.end() ||
                    pending->second->started ||
                    pending->second->priority != entry.priority)
                {
                    continue;
                }
                state = pending->second;
                state->started = true;
            }

            ImageData data = Load(entry.url);
            std::vector<Completion> completions;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_)
                    return;
                if (data)
                    InsertMemoryCache(entry.url, data);
                completions = std::move(state->completions);
                pending_.erase(entry.url);
            }

            for (auto& completion : completions)
            {
                if (completion)
                {
                    try
                    {
                        completion(data);
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<QueueEntry> critical_queue_;
    std::deque<QueueEntry> normal_queue_;
    std::deque<QueueEntry> prefetch_queue_;
    std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_;
    std::unordered_map<std::string, MemoryEntry> memory_cache_;
    std::vector<std::thread> workers_;
    size_t memory_cache_bytes_ = 0;
    std::uint64_t access_counter_ = 0;
    bool stopping_ = false;
};

std::mutex g_loader_mutex;
ImageLoader* g_loader = nullptr;

ImageLoader& Loader()
{
    std::lock_guard<std::mutex> lock(g_loader_mutex);
    if (!g_loader)
        g_loader = new ImageLoader();
    return *g_loader;
}

void SetCachedRemoteImage(
    brls::Image* image,
    const std::string& image_url,
    ImagePriority priority,
    std::function<bool()> should_apply = {})
{
    if (!image || image_url.empty())
        return;

    // Keep freeTexture=true. Replacing a resource-backed texture with an
    // async memory texture otherwise leaks GPU textures in Borealis.
    image->setImageAsync(
        [image_url, priority, should_apply = std::move(should_apply)](
            auto ready) mutable {
            Loader().Request(
                image_url,
                priority,
                [ready, should_apply = std::move(should_apply)](
                    ImageData data) mutable {
                    if (!data || (should_apply && !should_apply()))
                    {
                        ready({}, 0);
                        return;
                    }
                    ready(*data, data->size());
                });
        });
}

} // namespace

void SetCachedCoverImage(brls::Image* image, const std::string& image_url)
{
    if (!image)
        return;
    if (image_url.empty())
    {
        image->setImageFromRes(kFallbackCoverRes);
        return;
    }
    SetCachedRemoteImage(image, image_url, ImagePriority::Normal);
}

void SetCachedThumbnailImage(brls::Image* image, const std::string& image_url)
{
    if (!image)
        return;
    if (image_url.empty())
    {
        image->setImageFromRes(kFallbackCoverRes);
        return;
    }
    // NOTE: unlike GFN's img.nvidiagrid.net (which accepts resize query
    // params), Boosteroid's icon/bannerImage URLs have no confirmed resize
    // convention (TODO(protocol)) — served at their original size.
    SetCachedRemoteImage(image, image_url, ImagePriority::Normal);
}

void SetCachedGalleryImage(
    brls::Image* image,
    const std::string& image_url,
    std::function<bool()> should_apply)
{
    SetCachedRemoteImage(image, image_url, ImagePriority::Critical, std::move(should_apply));
}

void PrefetchCachedGalleryImage(const std::string& image_url)
{
    if (image_url.empty())
        return;
    Loader().Request(image_url, ImagePriority::Prefetch);
}

void SetCachedAvatarImage(brls::Image* image, const std::string& image_url)
{
    SetCachedRemoteImage(image, image_url, ImagePriority::Normal);
}

void ShutdownImageCache()
{
    ImageLoader* loader = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_loader_mutex);
        loader = std::exchange(g_loader, nullptr);
    }
    delete loader;
}

} // namespace opennow
