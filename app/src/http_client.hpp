#pragma once

#include <string>
#include <vector>

namespace opennow
{

struct HttpResponse
{
    long status_code = 0;
    std::string body;
    // Raw response header lines (e.g. "Set-Cookie: access_token=...; Path=/"),
    // in the order curl delivered them. Added for Boosteroid's cookie-session
    // auth (POST /api/v1/auth/login's Set-Cookie headers carry the session —
    // see boosteroid_client.cpp) — GFN's bearer-token auth never needed this.
    std::vector<std::string> headers;
};

class HttpClient
{
  public:
    HttpResponse Request(
        const std::string& method,
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers = {},
        const std::string& body = {},
        long connect_timeout_seconds = 10,
        long timeout_seconds = 30) const;

    HttpResponse Get(
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers = {},
        long connect_timeout_seconds = 10,
        long timeout_seconds = 30) const;

    HttpResponse Post(
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers,
        const std::string& body) const;

    int MeasureConnectLatencyMs(
        const std::string& url,
        long timeout_ms = 3000) const noexcept;
};

} // namespace opennow
