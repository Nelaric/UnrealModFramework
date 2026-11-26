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
	explicit APIGenerator(const fs::path& BuildInput);
	void Generate(const fs::path& OutputDirectory, const fs::path& Options, const fs::path& ClangLibrary,
	              const fs::path& LifecycleSource, const fs::path& BlueprintSnapshot,
	              const fs::path& ConfiguredSourceTool) &&;

private:
	GenerationInput Input;
};
} // namespace Mod::HeaderTool
