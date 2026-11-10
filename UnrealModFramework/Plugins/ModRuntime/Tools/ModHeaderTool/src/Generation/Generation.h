// Copyright (c) 2026 Nelaric Contributors
/** @file Generation.h */
#pragma once

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class Generation
{
public:
	/// Resolves active C++ APIs and publishes complete generated artifacts.
	static void Generate(const fs::path& BuildInput, const fs::path& OutputDirectory, const fs::path& Options,
	                     const fs::path& ClangLibrary, const fs::path& HostContexts,
	                     const fs::path& LifecycleSource = {});
};
} // namespace Mod::HeaderTool
