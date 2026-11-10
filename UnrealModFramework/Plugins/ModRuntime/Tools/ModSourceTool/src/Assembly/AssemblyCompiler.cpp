// Copyright (c) 2026 Nelaric Contributors
#include "AssemblyCompiler.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Process/Process.h"
#include "Common/Process/ProcessEnvironment.h"
#include "Common/Process/ResponseFile.h"
#include "ModSourceTool/src/Assembly/AssemblyFiles.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;

std::string AssemblyCompiler::CompileListing(const fs::path& Compiler, const fs::path& Directory,
                                             const std::vector<std::string>& Arguments,
                                             AssemblyCompiler::EDialect Dialect)
{
	const AssemblyFiles Files;
	auto Options = Arguments;
	if (Dialect == AssemblyCompiler::EDialect::Gnu)
	{
		Options.insert(Options.end(),
		               {"-S", "-g", "-masm=intel", "-o", Mod::BuildTool::Path::PathToUtf8(Files.Listing)});
	}
	else
	{
		Options.insert(Options.end(), {"/c", "/Fa" + Mod::BuildTool::Path::PathToUtf8(Files.Listing),
		                               "/Fo" + Mod::BuildTool::Path::PathToUtf8(Files.Object),
		                               "/Fd" + Mod::BuildTool::Path::PathToUtf8(Files.Debug)});
		if (Dialect == AssemblyCompiler::EDialect::Msvc)
		{
			Options.push_back("/FAsu");
		}
		else
		{
			Options.insert(Options.end(), {"/FA", "/Z7", "/clang:-masm=intel"});
		}
	}
	Mod::BuildTool::ResponseFile::WriteResponseFile(Files.Response, Options,
	                                                Dialect == AssemblyCompiler::EDialect::Gnu ? "gnu" : "msvc");
	auto Environment = Mod::BuildTool::ProcessEnvironment::CaptureEnvironment();
	Environment.erase("CL");
	Environment.erase("_CL_");
	const int Exit = Mod::BuildTool::Process::RunProcess(
	    Compiler, {"@" + Mod::BuildTool::Path::PathToUtf8(Files.Response)}, Directory, Environment);
	if (Exit != 0)
	{
		throw std::runtime_error(Mod::BuildTool::Path::PathToUtf8(Compiler) + " assembly generation failed (exit " +
		                         std::to_string(Exit) + ").");
	}
	return Mod::BuildTool::File::ReadFile(Files.Listing);
}
} // namespace Mod::SourceTool
