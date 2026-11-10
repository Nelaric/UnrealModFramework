// Copyright (c) 2026 Nelaric Contributors
/** @file HostLinkHeader.h */
#pragma once

#include <string>

namespace Mod::SourceTool
{
class HostLinkHeader
{
public:
	static void GenerateHostLink(const std::string& Owner, const std::string& Destination);
};
} // namespace Mod::SourceTool
