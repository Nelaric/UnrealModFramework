// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataBundle.h */
#pragma once

#include <filesystem>

namespace Mod::BuildTool
{
struct UeHookTarget;

class MetadataBundle
{
public:
	enum class EPhase
	{
		Both,
		Bootstrap,
		Export,
	};

	/// Prepares the Editor, exports metadata, or performs both phases.
	static void Prepare(const UeHookTarget& Producer, EPhase Phase);
	/// Checks a bundle against the specified project, engine and target.
	static void Validate(const UeHookTarget& Target);
	/// Validates a portable bundle and writes target-local inputs for UHT.
	static void Consume(const UeHookTarget& Target, const std::filesystem::path& Destination);
};
} // namespace Mod::BuildTool
