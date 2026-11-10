// Copyright (c) 2026 Nelaric Contributors
/** @file PortableNames.h */
#pragma once

#include <string>
#include <string_view>

namespace Mod::SdkTool
{
class PortableNames
{
public:
	static std::string Fold(std::string Value);
	static void Identifier(std::string_view Value);
	static void PortablePath(std::string_view Value);
};
} // namespace Mod::SdkTool
