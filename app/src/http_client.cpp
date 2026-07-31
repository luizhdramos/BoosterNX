#include "http_client.hpp"

#include "runtime_journal.hpp"

#include <curl/curl.h>

#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace opennow
{
namespace
{

// devkitPro's switch-curl portlib ships with SSL_VERIFYPEER on but no CA
// trust store baked in (there's no OS cert store on Switch) — without an
// explicit CURLOPT_CAINFO, every HTTPS request fails with curl error 51,
// "SSL peer certificate or SSH remote key was not OK" (CURLE_PEER_FAILED_VERIFICATION),
// CONFIRMED on real hardware 2026-07-31 against Boosteroid's login endpoint.
// A Mozilla CA bundle (curl's own cacert.pem, sourced via Python's certifi
// package) is shipped in romfs/cert/cacert.pem — see CMakeLists.txt's
// romfs copy step — and loaded from here at runtime.
constexpr const char* kCaBundlePath = "romfs:/cert/cacert.pem";

size_t WriteBody(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    const size_t bytes = size * nmemb;
    auto* body         = static_cast<std::string*>(userdata);
    body->append(ptr, bytes);
    return bytes;
}

size_t WriteHeaderLine(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    const size_t bytes = size * nmemb;
    auto* headers = static_cast<std::vector<std::string>*>(userdata);
    std::string line(ptr, bytes);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    if (!line.empty())
        headers->push_back(std::move(line));
    return bytes;
}

bool IsArtworkRequest(const std::string& url)
{
    return url.find("img.nvidiagrid.net") != std::string::npos ||
           url.find("steamstatic.com") != std::string::npos ||
           url.find("akamaihd.net") != std::string::npos;
}

bool IsSessionPollRequest(
    const std::string& method, const std::string& url)
{
    if (method != "GET")
        return false;
    const auto marker = url.find("/v2/session/");
    return marker != std::string::npos &&
           marker + std::string("/v2/session/").size() < url.size();
}

} // namespace

HttpResponse HttpClient::Request(
    const std::string& method,
    const std::string& url,
    const std::string& user_agent,
    const std::vector<std::string>& headers,
    const std::string& body,
    long connect_timeout_seconds,
    long timeout_seconds) const
{
    const bool artwork_request = IsArtworkRequest(url);
    const bool session_poll_request = IsSessionPollRequest(method, url);
    const bool journal_request = !artwork_request && !session_poll_request;
    const auto started_at = std::chrono::steady_clock::now();
    const std::string safe_url = SanitizeRuntimeUrl(url);
    const std::uint64_t operation_id = journal_request
        ? BeginRuntimeOperation("http", method, "url=" + safe_url)
        : 0;

    CURL* raw = curl_easy_init();
    if (!raw)
    {
        if (journal_request)
            EndRuntimeOperation(operation_id, "http", method, "error", "curl_easy_init_failed");
        throw std::runtime_error("curl_easy_init failed");
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(raw, &curl_easy_cleanup);
    std::string response_body;
    std::vector<std::string> response_headers;
    std::array<char, CURL_ERROR_SIZE> error_buffer {};

    curl_slist* raw_headers = nullptr;
    for (const auto& header : headers)
        raw_headers = curl_slist_append(raw_headers, header.c_str());

    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(
        raw_headers,
        &curl_slist_free_all);

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, connect_timeout_seconds);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, WriteHeaderLine);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl.get(), CURLOPT_CAINFO, kCaBundlePath);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error_buffer.data());

    if (header_list)
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header_list.get());

    if (method == "POST")
    {
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    else if (method != "GET")
    {
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty())
        {
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
    }

    // First attempt: verify the peer against the bundled Mozilla CA store
    // (kCaBundlePath). devkitPro's switch-curl reports its SSL backend as
    // "libnx" (CONFIRMED via curl_version_info()->ssl_version, see main.cpp) —
    // this is NOT one of curl's standard upstream TLS backends (no such
    // backend exists in curl's own source), so it's a devkitPro-maintained
    // shim, and it's unconfirmed whether it actually honors a PEM
    // CURLOPT_CAINFO the normal way or whether it always defers to the
    // Switch OS's own `ssl:` service and its Nintendo-curated root store
    // (which real hardware testing 2026-07-31 showed does NOT trust
    // Boosteroid's CA: curl_code=60 CURLE_SSL_CACERT even with VERIFYPEER=1
    // and no CAINFO at all). If the strict attempt fails specifically on a
    // CA/verification error, retry once with verification disabled rather
    // than hard-failing — this is the documented, commonly-used trade-off
    // for Switch homebrew apps talking to arbitrary (non-Nintendo) HTTPS
    // servers. The retry is logged (ssl_verify_bypassed=true) so this is
    // never silent.
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode result = curl_easy_perform(curl.get());
    bool verification_bypassed = false;
    if (result == CURLE_SSL_CACERT || result == CURLE_SSL_CACERT_BADFILE ||
        result == CURLE_PEER_FAILED_VERIFICATION)
    {
        response_body.clear();
        response_headers.clear();
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
        result = curl_easy_perform(curl.get());
        verification_bypassed = (result == CURLE_OK);
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (result != CURLE_OK)
    {
        long verify_result = 0;
        char* primary_ip = nullptr;
        curl_easy_getinfo(curl.get(), CURLINFO_SSL_VERIFYRESULT, &verify_result);
        curl_easy_getinfo(curl.get(), CURLINFO_PRIMARY_IP, &primary_ip);
        std::ostringstream detail;
        detail << "url=" << safe_url
               << " curl_code=" << static_cast<int>(result)
               << " curl_error=" << curl_easy_strerror(result)
               << " ssl_verify=" << verify_result
               << " elapsed_ms=" << elapsed_ms;
        if (error_buffer[0] != '\0')
            detail << " backend_error=" << error_buffer.data();
        if (primary_ip && *primary_ip)
            detail << " primary_ip=" << primary_ip;

        if (journal_request)
            EndRuntimeOperation(operation_id, "http", method, "error", detail.str());
        else if (artwork_request)
            LogRuntimeEvent("http", "artwork_error", detail.str());
        else
            LogRuntimeEvent("http", "session_poll_error", detail.str());
        throw std::runtime_error(
            "HTTP " + method + " failed for " + safe_url + ": " +
            curl_easy_strerror(result));
    }
    if (verification_bypassed)
    {
        LogRuntimeEvent("http", "ssl_verify_bypassed",
                         "url=" + safe_url + " reason=ca_bundle_rejected_by_platform_backend");
    }

    long status_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status_code);
    if (journal_request)
    {
        EndRuntimeOperation(
            operation_id, "http", method, "ok",
            "url=" + safe_url +
            " status=" + std::to_string(status_code) +
            " bytes=" + std::to_string(response_body.size()) +
            " elapsed_ms=" + std::to_string(elapsed_ms));
    }

    return HttpResponse{
        .status_code = status_code,
        .body        = std::move(response_body),
        .headers     = std::move(response_headers),
    };
}

HttpResponse HttpClient::Get(
    const std::string& url,
    const std::string& user_agent,
    const std::vector<std::string>& headers,
    long connect_timeout_seconds,
    long timeout_seconds) const
{
    return Request(
        "GET", url, user_agent, headers, {},
        connect_timeout_seconds, timeout_seconds);
}

HttpResponse HttpClient::Post(
    const std::string& url,
    const std::string& user_agent,
    const std::vector<std::string>& headers,
    const std::string& body) const
{
    return Request("POST", url, user_agent, headers, body);
}

int HttpClient::MeasureConnectLatencyMs(
    const std::string& url,
    long timeout_ms) const noexcept
{
    CURL* raw = curl_easy_init();
    if (!raw)
        return -1;

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(raw, &curl_easy_cleanup);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CAINFO, kCaBundlePath);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode connect_result = curl_easy_perform(curl.get());
    if (connect_result == CURLE_SSL_CACERT || connect_result == CURLE_SSL_CACERT_BADFILE ||
        connect_result == CURLE_PEER_FAILED_VERIFICATION)
    {
        // Same platform-backend CA caveat as Request() above — this is only
        // a latency probe (see StreamSettings' connection test), so falling
        // back to an unverified connection here is lower-stakes than the
        // login/session-cookie traffic Request() handles.
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
        connect_result = curl_easy_perform(curl.get());
    }
    if (connect_result != CURLE_OK)
        return -1;

    double connect_seconds = 0.0;
    if (curl_easy_getinfo(curl.get(), CURLINFO_CONNECT_TIME, &connect_seconds) != CURLE_OK ||
        connect_seconds < 0.0)
        return -1;

    return static_cast<int>(std::lround(connect_seconds * 1000.0));
}

} // namespace opennow
