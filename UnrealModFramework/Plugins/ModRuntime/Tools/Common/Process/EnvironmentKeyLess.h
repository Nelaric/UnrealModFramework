// Copyright (c) 2026 Nelaric Contributors
/** @file EnvironmentKeyLess.h */
#pragma once

#include <string>

namespace Mod::BuildTool
{
#ifdef _WIN32
struct EnvironmentKeyLess
{
	bool operator()(const std::wstring& Left, const std::wstring& Right) const;
};
#endif
} // namespace Mod::BuildTool
