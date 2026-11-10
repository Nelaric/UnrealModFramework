// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataOptionsRefreshRequired.h */
#pragma once

#include <stdexcept>
#include <string>

namespace Mod::HeaderTool
{
/// A missing or outdated snapshot can be exported again by UE.
class MetadataOptionsRefreshRequired : public std::runtime_error
{
public:
	MetadataOptionsRefreshRequired(std::string InReason, const char* Hint);

	/// The diagnostic without manual refresh instructions.
	const std::string Reason;
};
} // namespace Mod::HeaderTool
