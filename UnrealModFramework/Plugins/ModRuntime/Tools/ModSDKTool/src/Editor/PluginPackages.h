// Copyright (c) 2026 Nelaric Contributors
/** @file PluginPackages.h */
#pragma once

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

struct PluginPackages
{
	fs::path Sdk;
	fs::path Development;
};
} // namespace Mod::SdkTool
