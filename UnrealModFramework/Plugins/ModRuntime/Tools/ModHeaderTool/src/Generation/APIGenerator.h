// Copyright (c) 2026 Nelaric Contributors
/** @file APIGenerator.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationInput.h"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class APIGenerator
{
public:
	APIGenerator(const fs::path& BuildInput, const fs::path& HostContexts);
	void Generate(const fs::path& OutputDirectory, const fs::path& Options, const fs::path& ClangLibrary,
	              const fs::path& LifecycleSource, const fs::path& BlueprintSnapshot) &&;

private:
	fs::path ResolveAnalysisDatabase() const;

	GenerationInput Input;
	fs::path ContextFile;
	Json ContextSet;
};
} // namespace Mod::HeaderTool
