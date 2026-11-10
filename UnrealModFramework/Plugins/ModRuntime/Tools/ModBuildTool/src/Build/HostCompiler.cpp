// Copyright (c) 2026 Nelaric Contributors
#include "HostCompiler.h"

#include "Common/Files/Path.h"
#include "ModBuildTool/src/Archive/ArchiveInspector.h"
#include "ModBuildTool/src/Build/ArchiveWitness.h"
#include "ModBuildTool/src/Build/BuildRecipe.h"
#include "ModBuildTool/src/Build/CompilerDependencies.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

CompiledObjects HostCompiler::CompileObjects(const Request& Request, const fs::path& Work,
                                             const std::string& LinkWitness, fs::file_time_type Started)
{
	CompiledObjects Objects;
	for (std::size_t Index = 0; Index < Request.Sources.size(); ++Index)
	{
		const fs::path& Source = Request.Sources[Index];
		const fs::path Object = Work / (std::to_string(Index) + (Request.Platform == "Win64" ? ".obj" : ".o"));
		const fs::path Dependencies = Work / (std::to_string(Index) + ".deps");
		std::cerr << "ModBuildTool: compiling " << Path::PathToUtf8(Source) << '\n';
		BuildRecipe::ExecuteRecipe(Request, "compiler", Work / (std::to_string(Index) + ".rsp"),
		                           {{"{source}", Path::PathToUtf8(Source)},
		                            {"{object}", Path::PathToUtf8(Object)},
		                            {"{dependencies}", Path::PathToUtf8(Dependencies)}},
		                           {}, LinkWitness);
		if (!fs::is_regular_file(Object))
		{
			throw std::runtime_error("Compiler did not produce its declared object file.");
		}
		for (const fs::path& Dependency : CompilerDependencies::ReadCompilerDependencies(Request, Dependencies))
		{
			if (fs::last_write_time(Dependency) > Started)
			{
				throw std::runtime_error("InputsChanged: a compiler dependency changed during the build.");
			}
			Objects.Dependencies.insert(Dependency);
		}
		Objects.Paths.push_back(Object);
	}
	return Objects;
}

fs::path HostCompiler::CreateHostArchive(const Request& Request, const fs::path& Work,
                                         const std::vector<fs::path>& Objects, const std::string& LinkWitness)
{
	const fs::path Archive = Work / Request.Library.filename();
	BuildRecipe::ExecuteRecipe(Request, "archiver", Work / "archive.rsp", {{"{output}", Path::PathToUtf8(Archive)}},
	                           Objects);
	ArchiveInspector::ValidateHostArchive(Archive, Request.Platform, Request.Architecture,
	                                      ArchiveWitness::ArchiveSymbols(Request, LinkWitness));
	return Archive;
}
} // namespace Mod::BuildTool
