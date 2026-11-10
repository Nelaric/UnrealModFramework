// Copyright (c) 2026 Nelaric Contributors
/** @file ProcessArguments.h */
#pragma once

#include <string>
#include <string_view>

namespace Mod::SdkTool
{
#ifdef _WIN32
class ProcessArguments
{
public:
	static std::wstring Quote(std::string_view Value);
};
#endif
} // namespace Mod::SdkTool
