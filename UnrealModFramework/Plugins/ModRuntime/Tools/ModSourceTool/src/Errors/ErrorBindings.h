// Copyright (c) 2026 Nelaric Contributors
/** @file ErrorBindings.h */
#pragma once

#include "ModSourceTool/src/Errors/PendingBindings.h"

#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class ErrorBindings
{
public:
	static PendingBindings MatchReasons(Json& Failures, const Json& Reasons, Json& ClassifiedReasons, bool bStructured,
	                                    bool bUpdate);
	static void ApplyAssignedCodes(Json& BoundFailures, const Json& Reasons, const Json& ClassifiedReasons,
	                               bool bStructured);

private:
	[[noreturn]] static void FailGuard(const Json& Failure, const std::string& Message);
	static std::map<std::string, std::string> IndexGuardReasons(const Json& Reasons);
	static void MatchClassifications(const Json& Failure, Json& Classifications, Json& ClassifiedReasons, bool bUpdate,
	                                 PendingBindings& Pending);
};
} // namespace Mod::SourceTool
