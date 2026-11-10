// Copyright (c) 2026 Nelaric Contributors
#include "DebugLocations.h"

#include <string>

namespace Mod::SourceTool
{
std::string DebugLocations::File(unsigned Id) const
{
	const auto Found = Files.find(Id);
	return Found == Files.end() ? std::string{} : Found->second;
}
} // namespace Mod::SourceTool
