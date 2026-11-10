// Copyright (c) 2026 Nelaric Contributors
#include "CommandOptions.h"

#include "Common/Files/Path.h"

#include <filesystem>
#include <map>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Options = std::map<std::string, std::string>;

fs::path CommandOptions::OptionPath(const Options& Options, const char* Key)
{
	return Mod::BuildTool::Path::PathFromUtf8(Options.at(Key));
}

fs::path CommandOptions::ResultPath(const Options& Options)
{
	return Options.contains("--output") ? fs::weakly_canonical(fs::absolute(OptionPath(Options, "--output")))
	                                    : fs::path{};
}
} // namespace Mod::SdkTool
