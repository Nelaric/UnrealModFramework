// Copyright (c) 2026 Nelaric Contributors
/** @file CppLiteral.h */
#pragma once

#include <string>

namespace Mod::SdkTool
{
class CppLiteral
{
public:
	inline static const std::string LineDoc = "///";
	static std::string Literal(const std::string& Value);
};
} // namespace Mod::SdkTool
