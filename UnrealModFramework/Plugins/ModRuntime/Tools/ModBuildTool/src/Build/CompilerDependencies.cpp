// Copyright (c) 2026 Nelaric Contributors
#include "CompilerDependencies.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Build/ContextDocument.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::vector<fs::path> CompilerDependencies::ReadCompilerDependencies(const Request& Request, const fs::path& Path)
{
	std::vector<fs::path> Result;
	if (ContextDocument::Text(Request.Context.at("compiler"), "dependencyFormat") == "msvc-json")
	{
		const Json Data = ContextDocument::ReadJson(Path).at("Data");
		if ((!Data.value("ImportedModules", Json::array()).empty()) ||
		    (!Data.value("ImportedHeaderUnits", Json::array()).empty()))
		{
			throw std::runtime_error("C++ module/header-unit dependencies need a separate MBT profile.");
		}
		for (const Json& Include : Data.at("Includes"))
		{
			Result.push_back(Path::ResolvePath(Request.WorkingDirectory, Include.get<std::string>()));
		}
	}
	else
	{
		const std::string Contents = File::ReadFile(Path);
		std::string Rule;
		for (std::size_t Index = 0; Index < Contents.size(); ++Index)
		{
			if (Contents[Index] == '\\' && Index + 1 < Contents.size() &&
			    (Contents[Index + 1] == '\n' || Contents[Index + 1] == '\r'))
			{
				++Index;
				if (Contents[Index] == '\r' && Index + 1 < Contents.size() && Contents[Index + 1] == '\n')
				{
					++Index;
				}
				Rule.push_back(' ');
			}
			else if (Contents[Index] == '\n' || Contents[Index] == '\r')
			{
				break;
			}
			else
			{
				Rule.push_back(Contents[Index]);
			}
		}
		std::size_t Colon = std::string::npos;
		for (std::size_t Index = 0; Index < Rule.size(); ++Index)
		{
			if (Rule[Index] == '\\')
			{
				++Index;
			}
			else if (Rule[Index] == ':' &&
			         (Index + 1 == Rule.size() || std::isspace(static_cast<unsigned char>(Rule[Index + 1]))))
			{
				Colon = Index;
				break;
			}
		}
		if (Colon == std::string::npos)
		{
			throw std::runtime_error("Invalid Make dependency rule.");
		}
		std::string Token;
		for (std::size_t Index = Colon + 1; Index <= Rule.size(); ++Index)
		{
			if (Index < Rule.size() && Rule[Index] == '\\' && Index + 1 < Rule.size())
			{
				Token.push_back(Rule[++Index]);
			}
			else if (Index < Rule.size() && Rule[Index] == '$' && Index + 1 < Rule.size() && Rule[Index + 1] == '$')
			{
				Token.push_back('$');
				++Index;
			}
			else if (Index == Rule.size() || std::isspace(static_cast<unsigned char>(Rule[Index])))
			{
				if (!Token.empty())
				{
					Result.push_back(Path::ResolvePath(Request.WorkingDirectory, Token));
					Token.clear();
				}
			}
			else
			{
				Token.push_back(Rule[Index]);
			}
		}
	}
	return Result;
}

std::map<fs::path, std::string>
CompilerDependencies::HashCompilerDependencies(const std::set<fs::path>& CompilerDependencies,
                                               fs::file_time_type Started)
{
	std::map<fs::path, std::string> Dependencies;
	for (const fs::path& Path : CompilerDependencies)
	{
		if (fs::last_write_time(Path) > Started)
		{
			throw std::runtime_error("InputsChanged: a compiler dependency changed before publication.");
		}
		Dependencies[Path] = Hash::HashFile(Path);
	}
	return Dependencies;
}
} // namespace Mod::BuildTool
