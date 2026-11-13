// Copyright (c) 2026 Nelaric Contributors
/** @file API.h */
#pragma once

#include "ModHeaderTool/src/Generation/APIParameter.h"
#include "ModHeaderTool/src/Generation/APIType.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
struct BlueprintParameter
{
	std::string Name;
	std::string PropertyClass;
	std::string Role;
};

struct BlueprintFunction
{
	std::vector<BlueprintParameter> Parameters;
	std::size_t Index = 0;
};

struct API
{
	ExportMacro Macro;
	std::string Module;
	std::string Owner;
	std::string DeclaringType;
	std::string Name;
	std::string ExportName;
	std::string NameLocation;
	std::string Id;
	std::string Version;
	std::string Scope;
	std::vector<std::string> Permissions;
	APIType Return;
	std::vector<APIParameter> Parameters;
	std::string SourceSignature;
	std::string DeclarationUsr;
	std::optional<std::map<std::string, std::uint32_t>> SourceErrorCodes;
	std::string Symbol;
	std::optional<BlueprintFunction> Blueprint;
	bool bConst = false;
	bool bNoexcept = false;
	bool bFullViewMember = false;
	bool bReflected = false;
};
} // namespace Mod::HeaderTool
