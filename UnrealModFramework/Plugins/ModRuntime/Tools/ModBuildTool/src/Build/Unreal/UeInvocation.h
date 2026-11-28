// Copyright (c) 2026 Nelaric Contributors
/** @file UeInvocation.h */
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
struct UeHookTarget;

class UeInvocation
{
public:
	static std::filesystem::path EngineDirectory(const std::filesystem::path& Location);
	static std::filesystem::path Dotnet(const std::filesystem::path& Engine,
	                                    const std::map<std::string, std::string>& Environment);
	static std::filesystem::path Editor(const std::filesystem::path& Engine, const UeHookTarget& Target);
	static std::filesystem::path Receipt(const std::filesystem::path& Project, const UeHookTarget& Target);
	static void Run(const std::filesystem::path& Executable, const std::vector<std::string>& Arguments,
	                const std::filesystem::path& Directory, const std::map<std::string, std::string>& Environment,
	                std::string_view Stage);
};
} // namespace Mod::BuildTool
