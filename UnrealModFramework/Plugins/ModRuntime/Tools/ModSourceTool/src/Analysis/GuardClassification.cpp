// Copyright (c) 2026 Nelaric Contributors
#include "GuardClassification.h"

#include <nlohmann/json.hpp>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

Json GuardClassification::ClassifiedReason(const char* Key, const char* Macro, const char* Type)
{
	Json Reason = {{"key", Key}, {"macro", Macro}};
	if (Type)
	{
		Reason["type"] = Type;
	}
	return Reason;
}

Json GuardClassification::SkeletonMismatchReason()
{
	return ClassifiedReason("SkeletonMismatch", "MOD_ERROR_SKELETON_MISMATCH", "204");
}
} // namespace Mod::SourceTool
