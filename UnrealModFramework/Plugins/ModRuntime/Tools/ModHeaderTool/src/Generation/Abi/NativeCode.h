// Copyright (c) 2026 Nelaric Contributors
/** @file NativeCode.h */
#pragma once

#include <string>

namespace Mod::HeaderTool
{
class NativeCode
{
public:
	static std::string Literal(const std::string& Value);
	static std::string Upper(std::string Value);
	static std::string Source(const std::string& File);
};
} // namespace Mod::HeaderTool
