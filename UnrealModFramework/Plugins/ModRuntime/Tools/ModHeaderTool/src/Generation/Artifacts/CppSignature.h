// Copyright (c) 2026 Nelaric Contributors
/** @file CppSignature.h */
#pragma once

#include "ModHeaderTool/src/Generation/API.h"

#include <string>

namespace Mod::HeaderTool
{
class CppSignature
{
public:
	static std::string ResultType(const API& API);
	static std::string Parameters(const API& API, bool bSource = false, bool bOriginalNames = false);
	static std::string Arguments(const API& API, bool bSource = false);
	static std::string PointerType(const API& API);
};
} // namespace Mod::HeaderTool
