// Copyright (c) 2026 Nelaric Contributors
/** @file GuardClassification.h */
#pragma once

#include <nlohmann/json.hpp>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class GuardClassification
{
public:
	static Json ClassifiedReason(const char* Key, const char* Macro, const char* Type = nullptr);
	static Json SkeletonMismatchReason();
};
} // namespace Mod::SourceTool
