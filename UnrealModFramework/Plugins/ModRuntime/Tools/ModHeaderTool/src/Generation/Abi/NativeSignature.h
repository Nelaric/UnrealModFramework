// Copyright (c) 2026 Nelaric Contributors
/** @file NativeSignature.h */
#pragma once

#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/APIType.h"

#include <string>

namespace Mod::HeaderTool
{
class NativeSignature
{
public:
	static std::string Scalar(const APIType& Type);
	static bool IsBlueprintScalar(const APIType& Type);
	static std::string Parameters(const API& API, bool bNames);
	static std::string Arguments(const API& API, bool bSource);
	static std::string Member(const API& API);
};
} // namespace Mod::HeaderTool
