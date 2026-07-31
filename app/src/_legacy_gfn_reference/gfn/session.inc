SessionInfo GfnClient::StartSession(AuthSession& session, const std::string& launch_app_id,
                                    const std::string& launch_store,
                                    const std::string& internal_title) const
{
    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string device_id = device_id_;

    const StreamSettings stream_settings = LoadStreamSettings();
    ResetSessionTraceLog("StartSession appId=" + launch_app_id +
                         " store=" + (launch_store.empty() ? "unknown" : launch_store) +
                         " internalTitle=" + (internal_title.empty() ? "missing" : internal_title));

    std::string automatic_region_url;
    if (server_location::IsAutomatic(stream_settings.region))
    {
        const std::string provider_url =
            server_location::NormalizeStreamingBaseUrl(
                session.provider.streaming_service_url);
        {
            std::lock_guard<std::mutex> lock(g_region_selection_cache.mutex);
            const auto age =
                std::chrono::steady_clock::now() -
                g_region_selection_cache.measured_at;
            if (g_region_selection_cache.provider_url == provider_url &&
                age < kRegionSelectionCacheLifetime)
            {
                automatic_region_url =
                    g_region_selection_cache.selected_url;
            }
        }

        if (automatic_region_url.empty())
        {
            try
            {
                const std::vector<StreamRegion> measured =
                    MeasureStreamRegionLatencies(FetchStreamRegions(session));
                automatic_region_url =
                    server_location::SelectBestMeasuredRegionUrl(measured);

                if (!automatic_region_url.empty())
                {
                    std::lock_guard<std::mutex> lock(
                        g_region_selection_cache.mutex);
                    g_region_selection_cache.provider_url = provider_url;
                    g_region_selection_cache.selected_url =
                        automatic_region_url;
                    g_region_selection_cache.measured_at =
                        std::chrono::steady_clock::now();
                }
            }
            catch (const std::exception& e)
            {
                AppendSessionTraceLog(
                    "REGION automatic measurement failed; using provider endpoint error=" +
                    std::string(e.what()));
            }
        }
    }

    const std::string streaming_base_url =
        server_location::ResolveStreamingBaseUrl(
            stream_settings.region,
            session.provider.streaming_service_url,
            automatic_region_url);
    if (streaming_base_url.empty())
        throw std::runtime_error("No valid GeForce NOW streaming endpoint is available.");

    std::string url = streaming_base_url +
        "v2/session?keyboardLayout=en-US_qwerty&languageCode=" +
        stream_settings.game_language;

    std::vector<std::string> headers = {
        "Authorization: GFNJWT " + jwt_token,
        "Content-Type: application/json",
        "nv-client-id: " + client_id_,
        "nv-browser-type: CHROME",
        "nv-client-streamer: NVIDIA-CLASSIC",
        "nv-client-type: NATIVE",
        "nv-client-version: 30.0",
        "nv-device-make: UNKNOWN",
        "nv-device-model: UNKNOWN",
        "nv-device-os: WINDOWS",
        "nv-device-type: DESKTOP",
        "x-device-id: " + device_id,
        "Origin: https://play.geforcenow.com",
        "Referer: https://play.geforcenow.com/"
    };

    std::string body = BuildSessionBody(launch_app_id, internal_title, device_id, stream_settings);
    CleanupStaleCloudSession(session);
    jwt_token = ResolveSessionJwt(session);
    headers[0] = "Authorization: GFNJWT " + jwt_token;
    AppendSessionTraceLog("START url=" + url);
    AppendSessionTraceLog(
        "START region=" + stream_settings.region +
        " resolvedBase=" + streaming_base_url +
        (automatic_region_url.empty()
             ? ""
             : " measuredBest=" + automatic_region_url));
    AppendSessionTraceLog(
        "START settings=" + std::to_string(stream_settings.width) + "x" +
        std::to_string(stream_settings.height) + "@" + std::to_string(stream_settings.fps) +
        " bitrate=" + std::to_string(stream_settings.bitrate_kbps) +
        "kbps language=" + stream_settings.game_language +
        " persistGameSettings=" +
        std::to_string(stream_settings.persist_game_settings ? 1 : 0) +
        " deviceId=" + device_id);
    AppendSessionTraceLog("START headers:\n" + HeadersForTrace(headers));
    AppendSessionTraceLog("START request body:\n" + JsonForTrace(body));

    HttpResponse response = http_client_.Post(url, kUserAgent, headers, body);
    if (response.status_code == 401)
    {
        session = ForceRefreshSavedSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        AppendSessionTraceLog("START authorization rejected; refreshed token and retrying once");
        response = http_client_.Post(url, kUserAgent, headers, body);
    }
    AppendSessionTraceLog("START response HTTP " + std::to_string(response.status_code));
    AppendSessionTraceLog("START response body:\n" + JsonForTrace(response.body));

    auto parse_start_response = [&response]() -> JsonPtr {
        try {
            return LoadJson(response.body);
        } catch (const std::exception& e) {
            AppendSessionTraceLog(std::string("START JSON parse error: ") + e.what());
            return JsonPtr(nullptr, &json_decref);
        }
    };
    JsonPtr root = parse_start_response();

    if (root && IsDeviceSessionLimitResponse(root.get()))
    {
        AppendSessionTraceLog(
            "START device session limit detected; refreshing the server session list, "
            "cleaning matching sessions and retrying once");
        CleanupStaleCloudSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        response = http_client_.Post(url, kUserAgent, headers, body);
        AppendSessionTraceLog("START retry response HTTP " + std::to_string(response.status_code));
        AppendSessionTraceLog("START retry response body:\n" + JsonForTrace(response.body));
        root = parse_start_response();
    }

    const bool app_patching = root && IsAppPatchingResponse(root.get());
    if (root)
    {
        json_t* req_status = json_object_get(root.get(), "requestStatus");
        if (req_status && GetInteger(req_status, "statusCode") != 1 && !app_patching)
        {
            const std::string error = BuildCloudMatchErrorMessage("StartSession", response.status_code, root.get());
            AppendSessionTraceLog("START api error: " + error);
            throw std::runtime_error(error);
        }
    }

    if (response.status_code != 200 && response.status_code != 202 && !app_patching)
    {
        const std::string error = BuildCloudMatchErrorMessage("StartSession", response.status_code, root.get());
        AppendSessionTraceLog("START http error: " + error);
        throw std::runtime_error(error);
    }

    if (!root) {
        AppendSessionTraceLog("START failed: empty or invalid JSON");
        throw std::runtime_error("StartSession failed: Empty or invalid JSON" + SessionTraceHint());
    }

    SessionInfo info;
    info.app_patching = app_patching;
    CollectIceServersFromObject(root.get(), info);
    
    json_t* sess = json_object_get(root.get(), "session");
    if (sess)
    {
        json_t* s_id = json_object_get(sess, "sessionId");
        if (s_id && json_is_string(s_id))
            info.session_id = json_string_value(s_id);
            
        json_t* status = json_object_get(sess, "status");
        if (status)
            info.status = ParseSessionStatus(status);
            
        json_t* qp = json_object_get(sess, "queuePosition");
        if (qp && json_is_integer(qp))
            info.queue_position = json_integer_value(qp);

        ApplySeatSetupInfo(sess, info);

        ApplySessionNetworkInfo(sess, info);
    }
    
    // Check root for queuePosition
    json_t* root_qp = json_object_get(root.get(), "queuePosition");
    if (root_qp && json_is_integer(root_qp)) {
        info.queue_position = json_integer_value(root_qp);
    }

    if (info.session_id.empty()) {
        AppendSessionTraceLog("START failed: session ID missing after successful response");
        throw std::runtime_error("Failed to parse session ID from response" + SessionTraceHint());
    }

    if (app_patching)
        AppendSessionTraceLog("START pending reason=app_patching; polling created session");
    RememberActiveCloudSession(
        session,
        info.session_id,
        launch_app_id,
        streaming_base_url);
    AppendSessionTraceLog("START parsed: " + SessionInfoTraceSummary(info));
    return info;
}

SessionInfo GfnClient::PollSession(AuthSession& session, const std::string& session_id) const
{
    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string device_id = device_id_;
    const std::string active_base_url =
        LoadActiveCloudSessionBaseUrl(session, session_id);
    const std::string base_url = active_base_url.empty()
        ? server_location::ResolveStreamingBaseUrl(
              "Auto",
              session.provider.streaming_service_url)
        : active_base_url;
    std::string url = base_url + "v2/session/" + session_id;

    std::vector<std::string> headers = {
        "Authorization: GFNJWT " + jwt_token,
        "nv-client-id: " + client_id_,
        "nv-browser-type: CHROME",
        "nv-client-streamer: NVIDIA-CLASSIC",
        "nv-client-type: NATIVE",
        "nv-client-version: 30.0",
        "nv-device-make: UNKNOWN",
        "nv-device-model: UNKNOWN",
        "nv-device-os: WINDOWS",
        "nv-device-type: DESKTOP",
        "x-device-id: " + device_id,
        "Content-Type: application/json"
    };

    AppendSessionTraceLog("POLL url=" + url);
    AppendSessionTraceLog("POLL headers:\n" + HeadersForTrace(headers));
    HttpResponse response = http_client_.Get(url, kUserAgent, headers);
    if (response.status_code == 401)
    {
        session = ForceRefreshSavedSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        AppendSessionTraceLog("POLL authorization rejected; refreshed token and retrying once");
        response = http_client_.Get(url, kUserAgent, headers);
    }
    AppendSessionTraceLog("POLL response HTTP " + std::to_string(response.status_code));
    AppendSessionTraceLog("POLL response body:\n" + JsonForTrace(response.body));

    JsonPtr root(nullptr, &json_decref);
    try {
        root = LoadJson(response.body);
    } catch (const std::exception& e) {
        AppendSessionTraceLog(std::string("POLL JSON parse error: ") + e.what());
    }

    const bool app_patching = root && IsAppPatchingResponse(root.get());
    if (root)
    {
        json_t* req_status = json_object_get(root.get(), "requestStatus");
        if (req_status && GetInteger(req_status, "statusCode") != 1 && !app_patching)
        {
            const std::string error = BuildCloudMatchErrorMessage("PollSession", response.status_code, root.get());
            AppendSessionTraceLog("POLL api error: " + error);
            throw std::runtime_error(error);
        }
    }

    if (response.status_code != 200 && !app_patching)
    {
        const std::string error = BuildCloudMatchErrorMessage("PollSession", response.status_code, root.get());
        AppendSessionTraceLog("POLL http error: " + error);
        throw std::runtime_error(error);
    }

    if (!root) {
        AppendSessionTraceLog("POLL failed: empty or invalid JSON");
        throw std::runtime_error("PollSession failed: Empty or invalid JSON" + SessionTraceHint());
    }

    // Keep diagnostics useful without persisting session tokens returned by CloudMatch.
    const std::string redacted_session = JsonForTrace(response.body);
    FILE* session_dump = fopen("sdmc:/switch/SwitchNOW/switchnow_session.json", "w");
    if (session_dump) {
        fputs(redacted_session.c_str(), session_dump);
        fclose(session_dump);
    }

    SessionInfo info;
    info.session_id = session_id;
    info.app_patching = app_patching;
    CollectIceServersFromObject(root.get(), info);
    
    json_t* sess = json_object_get(root.get(), "session");
    if (sess)
    {
        json_t* status = json_object_get(sess, "status");
        if (status)
            info.status = ParseSessionStatus(status);
            
        json_t* qp = json_object_get(sess, "queuePosition");
        if (qp && json_is_integer(qp))
            info.queue_position = json_integer_value(qp);

        ApplySeatSetupInfo(sess, info);
            
        ApplySessionNetworkInfo(sess, info);
    }
    
    // Sometimes queuePosition is at the root level!
    json_t* root_qp = json_object_get(root.get(), "queuePosition");
    if (root_qp && json_is_integer(root_qp)) {
        info.queue_position = json_integer_value(root_qp);
    }

    if (app_patching)
        AppendSessionTraceLog("POLL pending reason=app_patching");
    AppendSessionTraceLog("POLL parsed: " + SessionInfoTraceSummary(info));
    return info;
}

void GfnClient::StopSession(
    AuthSession& session,
    const std::string& session_id,
    const std::string& server_ip) const
{
    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string device_id = device_id_;
    std::string base_url = LoadActiveCloudSessionBaseUrl(session, session_id);
    if (base_url.empty())
    {
        base_url = server_location::ResolveStreamingBaseUrl(
            "Auto",
            session.provider.streaming_service_url);
    }
    if (!server_ip.empty())
    {
        base_url = HasPrefix(server_ip, "https://") || HasPrefix(server_ip, "http://")
            ? server_ip
            : "https://" + server_ip;
        base_url = EnsureTrailingSlash(base_url);
    }
    std::string url = base_url + "v2/session/" + session_id;

    std::vector<std::string> headers = {
        "Authorization: GFNJWT " + jwt_token,
        "nv-client-id: " + client_id_,
        "nv-browser-type: CHROME",
        "nv-client-streamer: NVIDIA-CLASSIC",
        "nv-client-type: NATIVE",
        "nv-client-version: 30.0",
        "nv-device-make: UNKNOWN",
        "nv-device-model: UNKNOWN",
        "nv-device-os: WINDOWS",
        "nv-device-type: DESKTOP",
        "x-device-id: " + device_id,
        "Content-Type: application/json"
    };

    AppendSessionTraceLog("STOP url=" + url);
    AppendSessionTraceLog("STOP headers:\n" + HeadersForTrace(headers));
    HttpResponse response = http_client_.Request("DELETE", url, kUserAgent, headers);
    if (response.status_code == 401)
    {
        session = ForceRefreshSavedSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        AppendSessionTraceLog("STOP authorization rejected; refreshed token and retrying once");
        response = http_client_.Request("DELETE", url, kUserAgent, headers);
    }
    AppendSessionTraceLog("STOP response HTTP " + std::to_string(response.status_code));
    AppendSessionTraceLog("STOP response body:\n" + JsonForTrace(response.body));
    if ((response.status_code >= 200 && response.status_code < 300) || response.status_code == 404)
    {
        ForgetActiveCloudSession(session_id);
        return;
    }

    throw std::runtime_error("StopSession failed with HTTP " + std::to_string(response.status_code));
}

void GfnClient::CleanupStaleCloudSession(AuthSession& session) const
{
    const std::string stale_session_id = LoadActiveCloudSessionId(session);
    session = EnsureFreshSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string list_url = session.provider.streaming_service_url + "v2/session";
    std::vector<std::string> headers = {
        "Authorization: GFNJWT " + jwt_token,
        "nv-client-id: " + client_id_,
        "nv-browser-type: CHROME",
        "nv-client-streamer: NVIDIA-CLASSIC",
        "nv-client-type: NATIVE",
        "nv-client-version: 30.0",
        "nv-device-make: UNKNOWN",
        "nv-device-model: UNKNOWN",
        "nv-device-os: WINDOWS",
        "nv-device-type: DESKTOP",
        "x-device-id: " + device_id_,
        "Content-Type: application/json"
    };

    AppendSessionTraceLog("CLEANUP list url=" + list_url);
    HttpResponse response = http_client_.Get(list_url, kUserAgent, headers);
    if (response.status_code == 401)
    {
        session = ForceRefreshSavedSession(session);
        jwt_token = ResolveSessionJwt(session);
        headers[0] = "Authorization: GFNJWT " + jwt_token;
        AppendSessionTraceLog(
            "CLEANUP authorization rejected; refreshed token and retrying session list once");
        response = http_client_.Get(list_url, kUserAgent, headers);
    }
    AppendSessionTraceLog("CLEANUP list response HTTP " + std::to_string(response.status_code));
    AppendSessionTraceLog("CLEANUP list response body:\n" + JsonForTrace(response.body));

    bool usable_list = false;
    bool tracked_session_seen = false;
    std::string cleanup_error;
    if (response.status_code >= 200 && response.status_code < 300)
    {
        try
        {
            JsonPtr root = LoadJson(response.body);
            json_t* request_status = json_object_get(root.get(), "requestStatus");
            json_t* sessions = json_object_get(root.get(), "sessions");
            usable_list =
                request_status &&
                GetInteger(request_status, "statusCode") == 1 &&
                json_is_array(sessions);

            if (usable_list)
            {
                size_t index = 0;
                json_t* cloud_session = nullptr;
                json_array_foreach(sessions, index, cloud_session)
                {
                    const std::string session_id = GetString(cloud_session, "sessionId");
                    const int status = ParseSessionStatus(
                        json_object_get(cloud_session, "status"));
                    const std::string session_device_id = GetString(
                        json_object_get(cloud_session, "sessionRequestData"),
                        "deviceHashId");
                    if (session_id == stale_session_id)
                        tracked_session_seen = true;

                    if (!cloud_session::ShouldCleanup(
                            status,
                            session_id,
                            session_device_id,
                            device_id_,
                            stale_session_id))
                    {
                        continue;
                    }

                    const std::string server_ip = GetSessionServerIp(cloud_session);
                    AppendSessionTraceLog(
                        "CLEANUP stopping session id=" + session_id +
                        " status=" + std::to_string(status) +
                        " source=" +
                        (session_id == stale_session_id ? "local_record" : "matching_device") +
                        " server=" + (server_ip.empty() ? "provider" : server_ip));
                    try
                    {
                        StopSession(session, session_id, server_ip);
                    }
                    catch (const std::exception& e)
                    {
                        cleanup_error = e.what();
                        AppendSessionTraceLog(
                            "CLEANUP stop failed id=" + session_id +
                            " error=" + cleanup_error);
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            AppendSessionTraceLog(
                "CLEANUP list parse failed error=" + std::string(e.what()));
        }
    }

    if (usable_list)
    {
        if (!stale_session_id.empty() && !tracked_session_seen)
            ForgetActiveCloudSession(stale_session_id);
        if (!cleanup_error.empty())
            throw std::runtime_error(cleanup_error);
        return;
    }

    // Older or temporarily unavailable list endpoints still get the original
    // local-record cleanup path.
    if (!stale_session_id.empty())
    {
        AppendSessionTraceLog(
            "CLEANUP session list unavailable; falling back to locally tracked session");
        StopSession(session, stale_session_id);
    }
}
