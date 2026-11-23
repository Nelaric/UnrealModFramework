// Copyright (c) 2026 Nelaric Contributors
/** @file HostLinkHeader.h */
#pragma once

#include <string>
#include <span>

namespace Mod::SourceTool
{
class HostLinkHeader
{
public:
	static void GenerateHostLink(const std::string& Owner, const std::string& Destination,
	                             std::span<const std::string> HookWitnesses);
};
} // namespace Mod::SourceTool
