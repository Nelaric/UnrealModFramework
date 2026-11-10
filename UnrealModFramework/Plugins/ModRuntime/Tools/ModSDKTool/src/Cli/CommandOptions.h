// Copyright (c) 2026 Nelaric Contributors
/** @file CommandOptions.h */
#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Options = std::map<std::string, std::string>;

class CommandOptions
{
public:
	static fs::path OptionPath(const Options& Options, const char* Key);
	static fs::path ResultPath(const Options& Options);
};
} // namespace Mod::SdkTool
