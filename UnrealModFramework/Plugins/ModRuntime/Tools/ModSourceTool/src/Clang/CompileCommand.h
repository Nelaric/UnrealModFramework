// Copyright (c) 2026 Nelaric Contributors
/** @file CompileCommand.h */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;

struct CompileCommand
{
	fs::path Directory;
	std::string Compiler;
	std::vector<std::string> Arguments;
	std::vector<std::string> NativeArguments;
};
} // namespace Mod::SourceTool
