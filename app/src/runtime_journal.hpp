#pragma once

#include <cstdint>
#include <string>

namespace opennow
{

void InitializeRuntimeJournal(const std::string& app_version);
void ShutdownRuntimeJournal();
void LogRuntimeEvent(
    const std::string& category,
    const std::string& event,
    const std::string& detail = {});

std::uint64_t BeginRuntimeOperation(
    const std::string& category,
    const std::string& operation,
    const std::string& detail = {});
void EndRuntimeOperation(
    std::uint64_t operation_id,
    const std::string& category,
    const std::string& operation,
    const std::string& result,
    const std::string& detail = {});

std::string SanitizeRuntimeUrl(const std::string& url);
const std::string& RuntimeJournalPath();

} // namespace opennow
