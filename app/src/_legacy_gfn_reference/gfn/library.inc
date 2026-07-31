std::vector<GameInfo> GfnClient::FetchLibraryGames(AuthSession& session) const
{
    session = RecoverSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string vpc_id    = ResolveVpcId(http_client_, session);

    HttpResponse response = http_client_.Get(
        BuildLibraryUrl(vpc_id, true),
        kUserAgent,
        BuildGraphQlHeaders(jwt_token));

    if (response.status_code == 401)
    {
        session = RecoverSavedSession(session, true);
        jwt_token = ResolveSessionJwt(session);
        response = http_client_.Get(
            BuildLibraryUrl(vpc_id, true),
            kUserAgent,
            BuildGraphQlHeaders(jwt_token));
    }

    if (response.status_code != 200)
    {
        response = http_client_.Get(
            BuildLibraryUrl(vpc_id, false),
            kUserAgent,
            BuildGraphQlHeaders(jwt_token));
    }

    if (response.status_code != 200)
    {
        throw std::runtime_error(
            "Library fetch failed with HTTP " + std::to_string(response.status_code));
    }

    JsonPtr root = LoadJson(response.body);
    std::vector<GameInfo> games = ParseLibraryGames(root);
    const std::uint64_t metadata_operation = BeginRuntimeOperation(
        "catalog", "library_metadata",
        "games=" + std::to_string(games.size()));
    try
    {
        EnrichGamesWithMetadata(
            http_client_, jwt_token, vpc_id, games);
        EndRuntimeOperation(
            metadata_operation, "catalog", "library_metadata", "ok",
            "games=" + std::to_string(games.size()));
    }
    catch (const std::exception& error)
    {
        EndRuntimeOperation(
            metadata_operation, "catalog", "library_metadata", "fallback",
            error.what());
    }
    ApplyPlayHistory(games, LoadPlayHistory());
    return games;
}

bool GfnClient::LoadSavedSession(AuthSession& session) const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::string active_user_id;
    std::vector<AuthSession> sessions = LoadAccountsFromDisk(&active_user_id);
    if (!sessions.empty())
    {
        for (const AuthSession& saved : sessions)
        {
            if (saved.user.user_id == active_user_id)
            {
                session = saved;
                AppendAuthLog("auth: selected saved account " + AuthSessionHealth(session));
                return true;
            }
        }

        session = sessions.front();
        SaveAccountsToDisk(sessions, session.user.user_id);
        AppendAuthLog("auth: selected fallback account " + AuthSessionHealth(session));
        return true;
    }

    if (LoadLegacySession(session))
    {
        SaveSession(session);
        AppendAuthLog("auth: selected migrated legacy account " + AuthSessionHealth(session));
        return true;
    }

    return false;
}

std::vector<AuthSession> GfnClient::LoadSavedSessions() const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::vector<AuthSession> sessions = LoadAccountsFromDisk();
    if (!sessions.empty())
        return sessions;

    AuthSession legacy;
    if (LoadLegacySession(legacy))
    {
        SaveSession(legacy);
        sessions.push_back(std::move(legacy));
    }

    return sessions;
}

void GfnClient::SaveSession(const AuthSession& session) const
{
    if (!session.persistence_enabled)
        return;

    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    EnsureAppHome();

    std::vector<AuthSession> sessions = LoadAccountsFromDisk();
    bool replaced = false;
    for (AuthSession& saved : sessions)
    {
        if (saved.user.user_id == session.user.user_id)
        {
            saved = session;
            replaced = true;
            break;
        }
    }

    if (!replaced)
        sessions.push_back(session);

    SaveAccountsToDisk(sessions, session.user.user_id);
}

bool GfnClient::SetActiveSavedSession(const std::string& user_id) const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::vector<AuthSession> sessions = LoadAccountsFromDisk();
    for (const AuthSession& session : sessions)
    {
        if (session.user.user_id == user_id)
        {
            SaveAccountsToDisk(sessions, session.user.user_id);
            return true;
        }
    }

    return false;
}

void GfnClient::ClearSavedSession() const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    AuthSession active;
    if (!LoadSavedSession(active))
    {
        std::remove(GetSessionPath().c_str());
        std::remove((GetSessionPath() + ".bak").c_str());
        return;
    }

    std::vector<AuthSession> sessions = LoadAccountsFromDisk();
    sessions.erase(
        std::remove_if(
            sessions.begin(),
            sessions.end(),
            [&active](const AuthSession& session) {
                return session.user.user_id == active.user.user_id;
            }),
        sessions.end());

    if (sessions.empty())
    {
        std::remove(GetAccountsPath().c_str());
        std::remove((GetAccountsPath() + ".bak").c_str());
        std::remove(GetSessionPath().c_str());
        std::remove((GetSessionPath() + ".bak").c_str());
        return;
    }

    SaveAccountsToDisk(sessions, sessions.front().user.user_id);
}

void GfnClient::ClearAllSavedSessions() const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::remove(GetAccountsPath().c_str());
    std::remove((GetAccountsPath() + ".bak").c_str());
    std::remove(GetSessionPath().c_str());
    std::remove((GetSessionPath() + ".bak").c_str());
}

std::optional<NativeCredentials> GfnClient::LoadNativeCredentials(
    const std::string& provider_id) const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    for (NativeCredentials& credentials : LoadNativeCredentialEntries())
    {
        if (credentials.provider_id == provider_id)
            return credentials;
    }
    return std::nullopt;
}

void GfnClient::SaveNativeCredentials(const NativeCredentials& credentials) const
{
    if (credentials.provider_id.empty() || credentials.email.empty() || credentials.password.empty())
        throw std::runtime_error("Cannot save incomplete NVIDIA credentials");
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::vector<NativeCredentials> entries = LoadNativeCredentialEntries();
    bool replaced = false;
    for (NativeCredentials& saved : entries)
    {
        if (saved.provider_id == credentials.provider_id)
        {
            saved = credentials;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        entries.push_back(credentials);
    SaveNativeCredentialEntries(entries);
}

void GfnClient::ClearNativeCredentials(const std::string& provider_id) const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    std::vector<NativeCredentials> entries = LoadNativeCredentialEntries();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](const NativeCredentials& entry) {
            return entry.provider_id == provider_id;
        }),
        entries.end());
    SaveNativeCredentialEntries(entries);
    AppendAuthLog("auth-native: saved password removed");
}

void GfnClient::ClearAllNativeCredentials() const
{
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    SaveNativeCredentialEntries({});
    AppendAuthLog("auth-native: all saved passwords removed");
}

std::string GfnClient::LoadLauncherPreference(
    const std::string& user_id, const std::string& game_id) const
{
    if (user_id.empty() || game_id.empty())
        return "";
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    try {
        const std::string stored = ReadTextFile(GetLauncherPreferencesPath());
        if (stored.empty())
            return "";
        JsonPtr root = LoadJson(stored);
        return GetString(root.get(), (user_id + ":" + game_id).c_str());
    } catch (const std::exception& e) {
        AppendAuthLog("launcher: preference load failed error=" + std::string(e.what()));
        return "";
    }
}

void GfnClient::SaveLauncherPreference(
    const std::string& user_id, const std::string& game_id,
    const std::string& variant_id) const
{
    if (user_id.empty() || game_id.empty() || variant_id.empty())
        return;
    std::lock_guard<std::recursive_mutex> lock(g_accounts_mutex);
    try {
        JsonPtr root(json_object(), &json_decref);
        const std::string stored = ReadTextFile(GetLauncherPreferencesPath());
        if (!stored.empty()) {
            try {
                JsonPtr loaded = LoadJson(stored);
                if (json_is_object(loaded.get()))
                    root = std::move(loaded);
            } catch (...) {
            }
        }
        json_object_set_new(root.get(), (user_id + ":" + game_id).c_str(),
                            json_string(variant_id.c_str()));
        WriteJsonToFile(GetLauncherPreferencesPath(), root.get());
        AppendAuthLog("launcher: preference saved game=" + game_id + " variant=" + variant_id);
    } catch (const std::exception& e) {
        AppendAuthLog("launcher: preference save failed error=" + std::string(e.what()));
    }
}

[[maybe_unused]] static std::string GenerateUUID()
{
    auto bytes = GenerateRandomBytes(16);
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    std::string hex = HexEncode(bytes.data(), 16);
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" + hex.substr(20);
}

static std::string BuildSessionBody(
    const std::string& app_id,
    const std::string& internal_title,
    const std::string& device_id,
    const StreamSettings& stream_settings)
{
    json_t* root = json_object();
    json_t* req = json_object();
    try {
        json_object_set_new(req, "appId", json_integer(std::stoll(app_id)));
    } catch (...) {
        json_object_set_new(req, "appId", json_integer(0));
    }
    json_object_set_new(req, "cmsId", json_string(app_id.c_str()));
    if (internal_title.empty())
        json_object_set_new(req, "internalTitle", json_null());
    else
        json_object_set_new(req, "internalTitle", json_string(internal_title.c_str()));
    json_object_set_new(req, "networkTestSessionId", json_null());
    json_object_set_new(req, "parentSessionId", json_null());
    json_object_set_new(req, "clientIdentification", json_string("GFN-PC"));
    json_object_set_new(req, "deviceHashId", json_string(device_id.c_str()));
    json_object_set_new(req, "clientVersion", json_string("30.0"));
    json_object_set_new(req, "clientPlatformName", json_string("windows"));

    json_object_set_new(req, "availableSupportedControllers", json_array());
    json_object_set_new(req, "sdkVersion", json_string("1.0"));
    json_object_set_new(req, "streamerVersion", json_integer(1));
    json_object_set_new(req, "useOps", json_true());
    json_object_set_new(req, "audioMode", json_integer(2));
    json_object_set_new(req, "sdrHdrMode", json_integer(0));
    json_object_set_new(req, "clientDisplayHdrCapabilities", json_null());
    json_object_set_new(req, "surroundAudioInfo", json_integer(0));
    // Reserve player 1 for the virtual XInput controller exposed by the
    // Switch stream view. The gamepad reports use the same active bitmap.
    json_object_set_new(req, "remoteControllersBitmap", json_integer(1));
    json_object_set_new(req, "enhancedStreamMode", json_integer(1));
    json_object_set_new(req, "appLaunchMode", json_integer(1));
    json_object_set_new(req, "secureRTSPSupported", json_false());
    json_object_set_new(req, "partnerCustomData", json_string(""));
    json_object_set_new(req, "accountLinked", json_true());
    json_object_set_new(
        req, "enablePersistingInGameSettings",
        json_boolean(stream_settings.persist_game_settings));
    json_object_set_new(req, "userAge", json_integer(26));
    json_object_set_new(req, "clientTimezoneOffset", json_integer(0));
    
    json_t* features = json_object();
    json_object_set_new(features, "reflex", json_false());
    // CloudMatch uses an enum here: 0 = 8-bit SDR, 10 = 10-bit capable.
    // Sending literal 8 makes the whole launch request fail with a generic
    // INTERNAL_ERROR_STATUS before the session can enter queue/setup.
    json_object_set_new(features, "bitDepth", json_integer(0));
    json_object_set_new(features, "cloudGsync", json_false());
    json_object_set_new(features, "enabledL4S", json_false());
    json_object_set_new(features, "mouseMovementFlags", json_integer(0));
    json_object_set_new(features, "trueHdr", json_false());
    json_object_set_new(features, "supportedHidDevices", json_integer(0));
    json_object_set_new(features, "profile", json_integer(0));
    json_object_set_new(features, "fallbackToLogicalResolution", json_false());
    json_object_set_new(features, "hidDevices", json_null());
    json_object_set_new(features, "chromaFormat", json_integer(0));
    json_object_set_new(features, "prefilterMode", json_integer(0));
    json_object_set_new(features, "prefilterSharpness", json_integer(0));
    json_object_set_new(features, "prefilterNoiseReduction", json_integer(0));
    json_object_set_new(features, "hudStreamingMode", json_integer(0));
    json_object_set_new(features, "sdrColorSpace", json_integer(2));
    json_object_set_new(features, "hdrColorSpace", json_integer(0));
    json_object_set_new(req, "requestedStreamingFeatures", features);

    json_t* meta = json_array();
    json_t* m1 = json_object(); json_object_set_new(m1, "key", json_string("SubSessionId")); json_object_set_new(m1, "value", json_string(device_id.c_str())); json_array_append_new(meta, m1);
    json_t* m2 = json_object(); json_object_set_new(m2, "key", json_string("wssignaling")); json_object_set_new(m2, "value", json_string("1")); json_array_append_new(meta, m2);
    json_t* m3 = json_object(); json_object_set_new(m3, "key", json_string("GSStreamerType")); json_object_set_new(m3, "value", json_string("WebRTC")); json_array_append_new(meta, m3);
    json_object_set_new(req, "metaData", meta);

    json_t* monitors = json_array();
    json_t* monitor = json_object();
    json_object_set_new(monitor, "monitorId", json_integer(0));
    json_object_set_new(monitor, "positionX", json_integer(0));
    json_object_set_new(monitor, "positionY", json_integer(0));
    json_object_set_new(monitor, "widthInPixels", json_integer(stream_settings.width));
    json_object_set_new(monitor, "heightInPixels", json_integer(stream_settings.height));
    json_object_set_new(monitor, "framesPerSecond", json_integer(stream_settings.fps));
    json_object_set_new(monitor, "sdrHdrMode", json_integer(0));
    json_object_set_new(monitor, "displayData", json_null());
    json_object_set_new(monitor, "hdr10PlusGamingData", json_null());
    json_object_set_new(monitor, "dpi", json_integer(100));
    json_array_append_new(monitors, monitor);
    json_object_set_new(req, "clientRequestMonitorSettings", monitors);

    json_object_set_new(root, "sessionRequestData", req);

    char* dump = json_dumps(root, 0);
    std::string body(dump);
    free(dump);
    json_decref(root);
    return body;
}

int ParseSessionStatus(json_t* status)
{
    if (json_is_integer(status))
        return static_cast<int>(json_integer_value(status));

    if (!json_is_string(status))
        return -1;

    const std::string value = Lowercase(JsonString(status));
    if (value == "queued")
        return 0;

    if (value == "provisioning" || value == "initializing" || value == "setup" ||
        value == "setting_up" || value == "launching" || value == "launching_game")
        return 1;

    if (value == "active" || value == "ready" || value == "paused")
        return 2;

    if (value == "streaming" || value == "playing" || value == "connected")
        return 3;

    if (value.find("ad") != std::string::npos)
        return 6;

    if (value.find("fail") != std::string::npos || value.find("error") != std::string::npos ||
        value.find("closed") != std::string::npos || value.find("terminated") != std::string::npos ||
        value.find("cancel") != std::string::npos)
        return 4;

    return -1;
}

std::string SessionInfoTraceSummary(const SessionInfo& info)
{
    std::ostringstream out;
    out << "sessionId=" << info.session_id
        << " status=" << info.status
        << " queuePosition=" << info.queue_position
        << " appPatching=" << (info.app_patching ? 1 : 0)
        << " serverIp=" << info.server_ip
        << " signalingUrl=" << info.signaling_url
        << " media=" << info.media_ip << ":" << info.media_port
        << " iceServers=" << info.ice_servers.size()
        << " sessionToken=" << (info.session_token.empty() ? "missing" : "present");
    return out.str();
}
