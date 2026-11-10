// Copyright (c) 2026 Nelaric Contributors
/** @file GeneratedSource.h */
#pragma once

#include <string>

namespace Mod::HeaderTool
{
class GeneratedSource
{
public:
	static std::string Quote(const std::string& Text);
	static std::string Wide(const std::string& Text);
	static std::string Prologue(const std::string& File);
	static std::string Forward(const std::string& Name);
	static std::string APIMacro(std::string Module);
	static std::string SourceBytes(std::string Text);
};
} // namespace Mod::HeaderTool
