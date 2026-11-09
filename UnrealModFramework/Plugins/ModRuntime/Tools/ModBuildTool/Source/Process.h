// Copyright (c) 2026 Nelaric Contributors
/** @file Process.h */
#pragma once

#include "FileSystem.h"

#include <map>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
/// Captures the child environment before applying explicit context values.
std::map<std::string, std::string> CaptureEnvironment();
/// Executes an argument vector without a shell; child output goes to stderr.
int RunProcess(const fs::path& Executable, const std::vector<std::string>& Arguments, const fs::path& WorkingDirectory,
               const std::map<std::string, std::string>& Environment);
/// Encodes tool arguments in a compiler-compatible response file.
void WriteResponseFile(const fs::path& Path, const std::vector<std::string>& Arguments, std::string_view Format);
} // namespace Mod::BuildTool
