#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PeerConnection;

namespace opennow::webrtc
{

struct IceCandidatePairSnapshot {
    std::uint64_t priority = 0;
    int state = 0;
    int connectivity_checks = 0;
    bool selected = false;
    bool nominated = false;
    std::string local_type;
    std::string remote_type;
    std::string local_endpoint;
    std::string remote_endpoint;
};

std::vector<IceCandidatePairSnapshot> CaptureIceCandidatePairs(PeerConnection* pc);
std::string IceCandidatePairsSignature(
    const std::vector<IceCandidatePairSnapshot>& pairs);
std::vector<std::string> FormatIceCandidatePairs(
    const std::vector<IceCandidatePairSnapshot>& pairs);

} // namespace opennow::webrtc
