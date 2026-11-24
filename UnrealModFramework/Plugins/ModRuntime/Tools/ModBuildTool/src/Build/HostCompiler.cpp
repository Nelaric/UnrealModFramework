// Copyright (c) 2026 Nelaric Contributors
#include "HostCompiler.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
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

#ifdef _WIN32
#include <Windows.h>
#endif

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
		fs::path CompilerSource = Source;
		std::vector<std::string> LeadingArguments;
#ifdef _WIN32
		if (Source.native().size() >= MAX_PATH && Request.Context.at("compiler").at("responseFormat") == "msvc")
		{
			// MSVC needs a short input path even when Win32 can open the original source.
			CompilerSource = Work / (std::to_string(Index) + Source.extension().string());
			const auto Bytes = File::ReadFile(Source);
			if (Hash::HashBytes(Bytes) != Request.Inputs.at(Source))
			{
				throw std::runtime_error("InputsChanged: generated source changed before staging: " +
				                         Path::PathToUtf8(Source));
			}
			File::WriteFileAtomically(CompilerSource, Bytes);
			LeadingArguments.push_back("/I" + Path::PathToUtf8(Source.parent_path()));
		}
#endif
		std::cerr << "ModBuildTool: compiling " << Path::PathToUtf8(Source) << '\n';
		BuildRecipe::ExecuteRecipe(Request, "compiler", Work / (std::to_string(Index) + ".rsp"),
		                           {{"{source}", Path::PathToUtf8(CompilerSource)},
		                            {"{object}", Path::PathToUtf8(Object)},
		                            {"{dependencies}", Path::PathToUtf8(Dependencies)}},
		                           {}, LinkWitness, LeadingArguments);
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
	                           Objects, {}, {});
	ArchiveInspector::ValidateHostArchive(Archive, Request.Platform, Request.Architecture,
	                                      ArchiveWitness::ArchiveSymbols(Request, LinkWitness));
	return Archive;
}
} // namespace Mod::BuildTool
