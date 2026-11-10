// Copyright (c) 2026 Nelaric Contributors
/** @file ExportRedirects.h */
#pragma once

#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Metadata/FunctionRedirect.h"
#include "ModHeaderTool/src/Metadata/MetadataOptions.h"
#include "ModHeaderTool/src/Metadata/RedirectFunctionName.h"

#include <map>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
using RedirectsByTarget = std::multimap<std::string, const FunctionRedirect*>;

class ExportRedirects
{
public:
	static RedirectFunctionName ReflectedOwner(const ResolvedType& Type);
	static RedirectsByTarget CollectRedirects(const ResolvedType& Type, const RedirectFunctionName& Owner,
	                                          const MetadataOptions& Options);
	static void AddRedirectAliases(std::vector<API>& Aliases, const API& API, const ResolvedType& Type,
	                               const RedirectFunctionName& Owner, const RedirectsByTarget& Targets);

private:
	static bool MatchesOwner(const RedirectFunctionName& Name, const RedirectFunctionName& Owner);
	[[noreturn]] static void FailRedirect(const FunctionRedirect& Redirect, const std::string& Reason);
};
} // namespace Mod::HeaderTool
