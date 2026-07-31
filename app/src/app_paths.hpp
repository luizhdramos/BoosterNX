#pragma once

#include <string>

namespace opennow
{

const std::string& AppHomePath();
const std::string& LegacyAppHomePath();
const std::string& PreviousAppHomePath();
void PrepareAppStorage();

} // namespace opennow
