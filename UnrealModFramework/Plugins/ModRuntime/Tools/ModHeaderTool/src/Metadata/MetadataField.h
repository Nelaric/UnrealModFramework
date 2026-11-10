// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataField.h */
#pragma once

#include "ModHeaderTool/src/Metadata/MetadataValue.h"

#include <cstddef>
#include <string>

namespace Mod::HeaderTool
{
/// A named metadata value.
struct MetadataField
{
	/// Name of the declaration option.
	std::string Name;
	/// Byte position of the field name.
	std::size_t Offset = 0;
	/// Parsed field value.
	MetadataValue Value;
};
} // namespace Mod::HeaderTool
