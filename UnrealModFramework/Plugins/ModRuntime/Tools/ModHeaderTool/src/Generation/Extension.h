// Copyright (c) 2026 Nelaric Contributors
/** @file Extension.h */
#pragma once

#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::HeaderTool
{
struct GenerationModel;

struct Extension
{
	bool bHook = false;
	bool bPre = false;
	bool bPost = false;
	bool bAuthorityOnly = true;
	std::uint8_t Operations = 0;
	std::string Id;
	std::string Module;
	std::string Name;
	std::string Owner;
	std::string Version;
	std::string Permission;
	std::string PublishPermission;
	std::string BlueprintClass;
	std::string Input;
	std::string Output;
	std::string OutputRole;
	std::string SourceSignature;
	std::string Header;
	std::string Location;
	ExportMacro Macro;

	nlohmann::json Describe() const;
	std::string Signature() const;
};

void AddExtension(GenerationModel& Model, Extension Entry);
} // namespace Mod::HeaderTool
