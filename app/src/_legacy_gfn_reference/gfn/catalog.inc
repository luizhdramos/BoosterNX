GfnClient::GfnClient()
    : device_id_(NormalizeCloudDeviceId(GenerateDeviceId()))
{
    if (device_id_.empty())
        throw std::runtime_error("Unable to create a stable GeForce NOW device identity");
}

std::vector<StreamRegion> GfnClient::FetchStreamRegions(AuthSession& session) const
{
    session = RecoverSavedSession(session);
    const std::string base_url = server_location::ResolveStreamingBaseUrl(
        "Auto",
        session.provider.streaming_service_url);
    if (base_url.empty())
        throw std::runtime_error("The GeForce NOW provider has no valid streaming endpoint.");

    const HttpResponse response = http_client_.Get(
        base_url + "v2/serverInfo",
        kUserAgent,
        BuildGfnLcarsHeaders(
            ResolveSessionJwt(session),
            "BROWSER",
            "WEBRTC",
            true));
    if (response.status_code != 200)
    {
        throw std::runtime_error(
            "Could not load GeForce NOW server locations (HTTP " +
            std::to_string(response.status_code) + ").");
    }

    JsonPtr root = LoadJson(response.body);
    json_t* metadata = json_object_get(root.get(), "metaData");
    if (!json_is_array(metadata))
        return {};

    std::vector<StreamRegion> regions;
    std::unordered_set<std::string> seen_urls;
    size_t index = 0;
    json_t* entry = nullptr;
    json_array_foreach(metadata, index, entry)
    {
        const std::string name = Trim(GetString(entry, "key"));
        const std::string raw_url = Trim(GetString(entry, "value"));
        if (name.empty() || name == "gfn-regions" || name.rfind("gfn-", 0) == 0)
            continue;

        const std::string url =
            server_location::NormalizeStreamingBaseUrl(raw_url);
        if (url.empty() || !seen_urls.insert(url).second)
            continue;

        regions.push_back({name, url, -1});
    }

    std::sort(
        regions.begin(),
        regions.end(),
        [](const StreamRegion& left, const StreamRegion& right) {
            return left.name < right.name;
        });
    return regions;
}

std::vector<StreamRegion> GfnClient::MeasureStreamRegionLatencies(
    std::vector<StreamRegion> regions) const
{
    if (regions.empty())
        return regions;

    std::atomic<size_t> next_index {0};
    const size_t worker_count = std::min<size_t>(4, regions.size());
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (size_t worker = 0; worker < worker_count; ++worker)
    {
        workers.emplace_back([this, &regions, &next_index] {
            for (;;)
            {
                const size_t index =
                    next_index.fetch_add(1, std::memory_order_relaxed);
                if (index >= regions.size())
                    return;

                regions[index].ping_ms =
                    MeasureAverageLatency(http_client_, regions[index].url);
            }
        });
    }

    for (std::thread& worker : workers)
        worker.join();
    return regions;
}

std::vector<LoginProvider> GfnClient::FetchLoginProviders() const
{
    const HttpResponse response = http_client_.Get(
        kServiceUrlsEndpoint,
        kUserAgent,
        {"Accept: application/json"});

    if (response.status_code != 200)
    {
        throw std::runtime_error(
            "Provider discovery failed with HTTP " + std::to_string(response.status_code));
    }

    JsonPtr root = LoadJson(response.body);
    json_t* gfn_service_info =
        json_object_get(root.get(), "gfnServiceInfo");
    json_t* endpoints =
        gfn_service_info ? json_object_get(gfn_service_info, "gfnServiceEndpoints") : nullptr;

    std::vector<LoginProvider> providers;
    if (json_is_array(endpoints))
    {
        size_t index = 0;
        json_t* entry = nullptr;
        json_array_foreach(endpoints, index, entry)
        {
            LoginProvider provider;
            provider.idp_id                = GetString(entry, "idpId");
            provider.code                  = GetString(entry, "loginProviderCode");
            provider.display_name          = GetString(entry, "loginProviderDisplayName");
            provider.streaming_service_url = EnsureTrailingSlash(GetString(entry, "streamingServiceUrl"));
            provider.priority              = GetInteger(entry, "loginProviderPriority", 0);

            if (provider.code == "BPC")
                provider.display_name = "bro.game";

            if (!provider.idp_id.empty() && !provider.streaming_service_url.empty())
                providers.push_back(provider);
        }
    }

    if (providers.empty())
        providers.push_back(DefaultProvider());

    std::sort(
        providers.begin(),
        providers.end(),
        [](const LoginProvider& left, const LoginProvider& right) {
            return left.priority < right.priority;
        });

    return providers;
}

std::vector<PublicGame> GfnClient::FetchPublicGames() const
{
    const HttpResponse response = http_client_.Get(
        kPublicCatalogEndpoint,
        kUserAgent,
        {"Accept: application/json"});

    if (response.status_code != 200)
    {
        throw std::runtime_error(
            "Public catalog fetch failed with HTTP " + std::to_string(response.status_code));
    }

    JsonPtr root = LoadJson(response.body);
    if (!json_is_array(root.get()))
        throw std::runtime_error("Public catalog payload is not a JSON array");

    std::vector<PublicGame> games;

    size_t index = 0;
    json_t* item = nullptr;
    json_array_foreach(root.get(), index, item)
    {
        const std::string status = GetString(item, "status");
        const std::string title  = GetString(item, "title");
        if (status != "AVAILABLE" || title.empty())
            continue;

        PublicGame game;
        game.id        = GetString(item, "id");
        game.title     = title;
        game.store     = InferStore(item);
        game.publisher = GetString(item, "publisher");
        game.developer = GetString(item, "developer");
        game.genres = ParseLabelArray(json_object_get(item, "genres"));
        game.image_url = BuildSteamImageUrl(GetString(item, "steamUrl"));
        game.poster_url = game.image_url;
        game.description = GetString(item, "description");
        if (game.description.empty())
            game.description = GetString(item, "longDescription");
        if (game.id.empty())
            game.id = game.title;

        games.push_back(std::move(game));
    }

    std::sort(
        games.begin(),
        games.end(),
        [](const PublicGame& left, const PublicGame& right) {
            return left.title < right.title;
        });

    return games;
}

std::vector<PublicGame> GfnClient::FetchCatalogGames(
    AuthSession& session, const std::string& search_query) const
{
    session = RecoverSavedSession(session);
    std::string jwt_token = ResolveSessionJwt(session);
    const std::string vpc_id = ResolveVpcId(http_client_, session);

    std::vector<PublicGame> games;
    std::unordered_set<std::string> seen_ids;
    std::string cursor;

    for (int page = 0; page < 3; ++page)
    {
        HttpResponse response = http_client_.Post(
            kGraphQlEndpoint,
            kUserAgent,
            BuildGraphQlPostHeaders(jwt_token),
            BuildCatalogRequestBody(vpc_id, search_query, cursor));

        if (response.status_code == 401)
        {
            session = RecoverSavedSession(session, true);
            jwt_token = ResolveSessionJwt(session);
            response = http_client_.Post(
                kGraphQlEndpoint,
                kUserAgent,
                BuildGraphQlPostHeaders(jwt_token),
                BuildCatalogRequestBody(vpc_id, search_query, cursor));
        }

        if (response.status_code != 200)
        {
            throw std::runtime_error(
                "Catalog browse failed with HTTP " + std::to_string(response.status_code));
        }

        JsonPtr root = LoadJson(response.body);
        bool has_next_page = false;
        std::string end_cursor;
        std::vector<PublicGame> page_games =
            ParseCatalogPage(root, has_next_page, end_cursor);
        for (PublicGame& game : page_games)
        {
            const std::string key = game.id.empty() ? game.title : game.id;
            if (seen_ids.insert(key).second)
                games.push_back(std::move(game));
        }

        if (!has_next_page || end_cursor.empty() || end_cursor == cursor)
            break;
        cursor = std::move(end_cursor);
    }

    return games;
}

AuthSession GfnClient::Login(const LoginProvider& provider, const std::string& login_hint) const
{
    AppendAuthLog("auth: login started provider=" + provider.code);

    const PkcePair pkce = GeneratePkce();
    const std::string state = HexEncode(GenerateRandomBytes(32).data(), 32);
    const std::string auth_url = BuildAuthorizeUrl(provider, pkce, state, kRedirectUri, login_hint);
    const std::string final_url = RunBrowserLogin(auth_url);

    if (GetQueryValue(final_url, "state") != state)
        throw std::runtime_error("GeForce NOW login callback failed the OAuth state check");

    const std::string error = GetQueryValue(final_url, "error");
    if (!error.empty())
    {
        throw std::runtime_error("GeForce NOW login failed: " + error);
    }

    const std::string code = GetQueryValue(final_url, "code");
    if (code.empty())
        throw std::runtime_error("The GeForce NOW login callback did not include an authorization code");

    AuthTokens tokens = ExchangeAuthorizationCode(http_client_, code, pkce.verifier);
    AppendAuthLog("auth: token exchange ok");
    RequestClientToken(http_client_, tokens);

    AuthSession session;
    session.provider = provider;
    session.tokens   = tokens;
    session.user     = FetchUserInfo(http_client_, tokens);
    session.last_refresh_at_ms = NowMs();
    AppendAuthLog("auth: user info ok user_id_present=" + std::string(session.user.user_id.empty() ? "no" : "yes"));
    return session;
}
