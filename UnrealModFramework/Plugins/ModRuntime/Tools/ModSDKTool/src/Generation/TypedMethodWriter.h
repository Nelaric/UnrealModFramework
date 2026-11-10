// Copyright (c) 2026 Nelaric Contributors
/** @file TypedMethodWriter.h */
#pragma once

#include "ModSDKTool/src/Generation/WrapperErrors.h"

#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

class TypedMethodWriter
{
public:
	static void WriteTypedMethod(std::ostringstream& Header, std::ostringstream& Source, const Json& API,
	                             const std::map<std::string, std::string>& Types, const std::string& Context,
	                             const std::string& Outcome, const std::string& LibraryClass,
	                             const std::string& APIMacro, const std::string& Plugin, const WrapperErrors& Errors);

private:
	static std::string NativeScalar(const std::string& Type);
};
} // namespace Mod::SdkTool
