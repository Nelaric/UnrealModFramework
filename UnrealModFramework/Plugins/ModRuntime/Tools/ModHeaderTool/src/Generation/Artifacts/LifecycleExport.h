// Copyright (c) 2026 Nelaric Contributors
/** @file LifecycleExport.h */
#pragma once

#include <filesystem>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class LifecycleExport
{
public:
	static constexpr const char* LifecyclePath = "Contract/ModLifecycle.json";
	static constexpr const char* LifecycleClasses[] = {"ModInstance", "ModInstanceSubSystem"};
	static std::string ExportLifecycleDeclarations(const fs::path& Source);
};
} // namespace Mod::HeaderTool
