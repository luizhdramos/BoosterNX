#include "IceDiagnostics.hpp"

#include "peer_connection.h"

#include <algorithm>
#include <sstream>

namespace opennow::webrtc
{
namespace
{

const char* StateName(int state)
{
    switch (state)
    {
        case 0: return "frozen";
        case 1: return "waiting";
        case 2: return "checking";
        case 3: return "succeeded";
        case 4: return "failed";
        default: return "unknown";
    }
}

const char* CandidateTypeName(int type)
{
    switch (type)
    {
        case 0: return "host";
        case 1: return "srflx";
        case 2: return "prflx";
        case 3: return "relay";
        default: return "unknown";
    }
}

std::string Endpoint(const char* address, int port)
{
    std::string value = address && *address ? address : "?";
    if (value.find(':') != std::string::npos)
        value = "[" + value + "]";
    return value + ":" + std::to_string(port);
}

} // namespace

std::vector<IceCandidatePairSnapshot> CaptureIceCandidatePairs(PeerConnection* pc)
{
    std::vector<IceCandidatePairSnapshot> result;
    const int count = std::max(0, peer_connection_get_ice_candidate_pair_count(pc));
    result.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index)
    {
        PeerIceCandidatePairInfo info {};
        if (peer_connection_get_ice_candidate_pair_info(pc, index, &info) != 0)
            continue;

        IceCandidatePairSnapshot pair;
        pair.priority = info.priority;
        pair.state = info.state;
        pair.connectivity_checks = info.connectivity_checks;
        pair.selected = info.selected != 0;
        pair.nominated = info.nominated != 0;
        pair.local_type = CandidateTypeName(info.local_type);
        pair.remote_type = CandidateTypeName(info.remote_type);
        pair.local_endpoint = Endpoint(info.local_address, info.local_port);
        pair.remote_endpoint = Endpoint(info.remote_address, info.remote_port);
        result.push_back(std::move(pair));
    }
    return result;
}

std::string IceCandidatePairsSignature(
    const std::vector<IceCandidatePairSnapshot>& pairs)
{
    std::ostringstream out;
    for (const auto& pair : pairs)
    {
        out << pair.priority << ':' << pair.state << ':'
            << pair.connectivity_checks << ':' << pair.selected << ':'
            << pair.nominated << ':' << pair.local_type << ':'
            << pair.remote_type << ':' << pair.local_endpoint << ':'
            << pair.remote_endpoint << ';';
    }
    return out.str();
}

std::vector<std::string> FormatIceCandidatePairs(
    const std::vector<IceCandidatePairSnapshot>& pairs)
{
    std::vector<std::string> lines;
    lines.reserve(pairs.size() + 1);
    lines.push_back("ICE candidate-pair snapshot count=" +
                    std::to_string(pairs.size()));
    for (std::size_t index = 0; index < pairs.size(); ++index)
    {
        const auto& pair = pairs[index];
        lines.push_back(
            "ICE pair[" + std::to_string(index) + "] state=" +
            StateName(pair.state) + " local=" + pair.local_type + "/" +
            pair.local_endpoint + " remote=" + pair.remote_type + "/" +
            pair.remote_endpoint + " priority=" +
            std::to_string(pair.priority) + " checks=" +
            std::to_string(pair.connectivity_checks) + " selected=" +
            std::to_string(pair.selected ? 1 : 0) + " nominated=" +
            std::to_string(pair.nominated ? 1 : 0));
    }
    return lines;
}

} // namespace opennow::webrtc
