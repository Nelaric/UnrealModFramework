// Copyright (c) 2026 Nelaric Contributors
/** @file AssemblyCompiler.h */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;

class AssemblyCompiler
{
public:
	enum class EDialect
	{
		Msvc,
		ClangCL,
		Gnu
	};

	static std::string CompileListing(const fs::path& Compiler, const fs::path& Directory,
	                                  const std::vector<std::string>& Arguments, EDialect Dialect);
};
} // namespace Mod::SourceTool
