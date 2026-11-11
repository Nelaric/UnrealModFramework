// Copyright (c) 2026 Nelaric Contributors
#include "CompilationDatabase.h"

#include "Common/Files/Path.h"

#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;

CompileCommand CompilationDatabase::LoadCompileCommand(const Clang& Clang, const SourceRequest& Request)
{
	if (Request.Database.filename() != "compile_commands.json" || !fs::is_regular_file(Request.Database))
	{
		throw std::runtime_error("compilationDatabase must name an existing compile_commands.json.");
	}
	CXCompilationDatabase_Error Error{};
	const std::unique_ptr<void, decltype(Clang.clang_CompilationDatabase_dispose)> Database(
	    Clang.clang_CompilationDatabase_fromDirectory(
	        Mod::BuildTool::Path::PathToUtf8(Request.Database.parent_path()).c_str(), &Error),
	    Clang.clang_CompilationDatabase_dispose);
	if (!Database)
	{
		throw std::runtime_error("Cannot load compilation database: " +
		                         Mod::BuildTool::Path::PathToUtf8(Request.Database));
	}

	// Enumerate actual entries. A per-file lookup may infer a command for a missing source.
	const std::unique_ptr<void, decltype(Clang.clang_CompileCommands_dispose)> Commands(
	    Clang.clang_CompilationDatabase_getAllCompileCommands(Database.get()), Clang.clang_CompileCommands_dispose);
	CXCompileCommand Selected = nullptr;
	fs::path Directory;
	for (unsigned Index = 0; Commands && Index < Clang.clang_CompileCommands_getSize(Commands.get()); ++Index)
	{
		const auto Command = Clang.clang_CompileCommands_getCommand(Commands.get(), Index);
		const auto WorkingDir =
		    Mod::BuildTool::Path::PathFromUtf8(Clang.String(Clang.clang_CompileCommand_getDirectory(Command)));
		const auto File = WorkingDir / Mod::BuildTool::Path::PathFromUtf8(
		                                   Clang.String(Clang.clang_CompileCommand_getFilename(Command)));
		std::error_code PathError;
		if (fs::equivalent(File, Request.Source, PathError))
		{
			if (Selected)
			{
				throw std::runtime_error("Multiple compile commands for " +
				                         Mod::BuildTool::Path::PathToUtf8(Request.Source));
			}
			Selected = Command;
			Directory = fs::canonical(WorkingDir);
		}
	}
	if (!Selected || Clang.clang_CompileCommand_getNumArgs(Selected) < 2)
	{
		throw std::runtime_error("No actual compile command for " + Mod::BuildTool::Path::PathToUtf8(Request.Source));
	}

	CompileCommand Result{.Directory = Directory,
	                      .Compiler = Clang.String(Clang.clang_CompileCommand_getArg(Selected, 0)),
	                      .Arguments = {},
	                      .NativeArguments = {}};
	const auto Driver = Mod::BuildTool::Path::PathFromUtf8(Result.Compiler).filename();
	const auto DriverName = Mod::BuildTool::Path::PathToUtf8(Driver);
	const bool bMsvcStyle = Driver == "cl.exe" || Driver == "clang-cl.exe";
	if (!bMsvcStyle && DriverName.find("clang") == std::string::npos && DriverName.find("g++") == std::string::npos &&
	    DriverName != "c++")
	{
		throw std::runtime_error("Unsupported C++ compiler in the compilation database: " + Result.Compiler);
	}
	Result.Arguments = {Result.Compiler, bMsvcStyle ? "--driver-mode=cl" : "--driver-mode=g++"};
	const unsigned Count = Clang.clang_CompileCommand_getNumArgs(Selected);
	for (unsigned Index = 1; Index < Count; ++Index)
	{
		std::string Arg = Clang.String(Clang.clang_CompileCommand_getArg(Selected, Index));
		if (Arg.starts_with('@') || Arg.starts_with("/Yu") || Arg.starts_with("/Yc"))
		{
			throw std::runtime_error("Regenerate the UBT database with -NoPCH and keep its response files available.");
		}
		if (Arg == "/c" || Arg == "-c" || Arg == "-S" || Arg == "--driver-mode=cl" || Arg == "--driver-mode=g++" ||
		    Arg == "-MD" || Arg == "-MMD" || Arg == "-MP")
		{
			continue;
		}
		if (Arg == "/experimental:log" || Arg == "/sourceDependencies" || Arg == "/Fo" || Arg == "/Fd" ||
		    Arg == "/Fa" || Arg == "-o" || Arg == "-MF" || Arg == "-MT" || Arg == "-MQ" || Arg == "-MJ")
		{
			if (++Index == Count)
			{
				throw std::runtime_error("Missing compile command argument after " + Arg);
			}
			continue;
		}
		if ((bMsvcStyle &&
		     (Arg.starts_with("/Fo") || Arg.starts_with("/Fd") || Arg.starts_with("/Fa") || Arg.starts_with("/FA"))) ||
		    Arg.starts_with("-MF") || Arg.starts_with("-MT") || Arg.starts_with("-MQ") || Arg.starts_with("-MJ"))
		{
			continue;
		}
		Result.NativeArguments.push_back(std::move(Arg));
	}
	Result.Arguments.insert(Result.Arguments.end(), Result.NativeArguments.begin(), Result.NativeArguments.end());
	const std::string Prefix = bMsvcStyle ? "/clang:" : "";
	Result.Arguments.insert(Result.Arguments.end(),
	                        {Prefix + "-working-directory", Prefix + Mod::BuildTool::Path::PathToUtf8(Directory),
	                         "-resource-dir=" + Mod::BuildTool::Path::PathToUtf8(Request.ResourceDir),
	                         Prefix + "-Wno-invalid-constexpr", bMsvcStyle ? "/Zs" : "-fsyntax-only"});
	for (const auto& Argument : Request.AnalysisArguments)
	{
		Result.Arguments.push_back(Prefix + Argument);
	}
	return Result;
}
} // namespace Mod::SourceTool
