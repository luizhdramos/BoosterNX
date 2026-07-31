#pragma once

// Small shared jansson helpers, factored out of SwitchNOW's gfn_client.cpp
// (where they were file-local) since three separate Boosteroid protocol
// files (boosteroid_client, boosteroid_signaling_client,
// boosteroid_control_channel) all need the same JSON plumbing.

#include <jansson.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace opennow::json
{

using JsonPtr = std::unique_ptr<json_t, decltype(&json_decref)>;

inline JsonPtr Load(const std::string& body)
{
    json_error_t error {};
    JsonPtr root(json_loads(body.c_str(), 0, &error), &json_decref);
    if (!root)
        throw std::runtime_error("JSON parse failed at line " + std::to_string(error.line) + ": " + error.text);
    return root;
}

// Non-throwing variant: returns a null JsonPtr instead of throwing, for
// call sites that treat "not JSON yet" as a retryable state (e.g. polling a
// still-booting node) rather than a hard error.
inline JsonPtr TryLoad(const std::string& body) noexcept
{
    json_error_t error {};
    return JsonPtr(json_loads(body.c_str(), 0, &error), &json_decref);
}

inline std::string AsString(json_t* value)
{
    if (json_is_string(value))
    {
        const char* raw = json_string_value(value);
        return raw ? raw : "";
    }
    if (json_is_integer(value))
        return std::to_string(json_integer_value(value));
    return "";
}

inline std::string GetString(json_t* object, const char* key)
{
    if (!object || !json_is_object(object))
        return "";
    return AsString(json_object_get(object, key));
}

inline bool GetBool(json_t* object, const char* key, bool fallback = false)
{
    if (!object || !json_is_object(object))
        return fallback;
    json_t* value = json_object_get(object, key);
    return json_is_boolean(value) ? json_boolean_value(value) : fallback;
}

inline int GetInteger(json_t* object, const char* key, int fallback = 0)
{
    if (!object || !json_is_object(object))
        return fallback;
    json_t* value = json_object_get(object, key);
    return json_is_integer(value) ? static_cast<int>(json_integer_value(value)) : fallback;
}

// Boosteroid's `gw` field has been observed as either a bare string
// ("https://sp7.cloud.boosteroid.com:443") or an object ({"address": "..."})
// depending on endpoint (session/details vs. the v2 session/start 201 body /
// streaming.js's own gateway list) — CONFIRMED 2026-07-24, see
// BoosteroidSessionDetailsSuccessDTO.Payload's doc comment in BoosteroidATV's
// SessionState.swift. Decode leniently rather than picking one shape.
inline std::string GetGatewayAddress(json_t* object, const char* key)
{
    if (!object || !json_is_object(object))
        return "";
    json_t* value = json_object_get(object, key);
    if (json_is_string(value))
    {
        const char* raw = json_string_value(value);
        return raw ? raw : "";
    }
    if (json_is_object(value))
        return GetString(value, "address");
    return "";
}

} // namespace opennow::json
