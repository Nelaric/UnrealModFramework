// Copyright (c) 2026 Nelaric Contributors
/** @file Validator.h */
#pragma once

#include "ModHeaderTool/src/Metadata/MetadataOptions.h"
#include "ModHeaderTool/src/Metadata/MetadataValue.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"
#include "ModHeaderTool/src/Scan/Lexer.h"

#include "ModExportMetadataSchema.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::HeaderTool
{
namespace Schema = Mod::ExportMetadata::Schema;

class Validator
{
public:
	Validator(ExportMacro& InMacro, const MetadataOptions* InOptions, const std::string& Source);

	std::vector<std::string> Validate();

private:
	static bool IsVersion(std::string_view Text);

	static bool IsTypeName(std::string_view Text);

	void Error(std::size_t Offset, const std::string& Reason);

	void ValidateHookPolicy();

	void CheckChoice(const Schema::FFieldSpec& Spec, const MetadataValue& Value);

	void ValidateValue(const Schema::FFieldSpec& Spec, const MetadataValue& Value);

	ExportMacro& Macro;
	const MetadataOptions* Options;
	Detail::Lexer Locations;
	std::vector<std::string> Diagnostics;
};
} // namespace Mod::HeaderTool
