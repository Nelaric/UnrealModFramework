// Copyright (c) 2026 Nelaric Contributors
/** @file ContractSchema.h */
#pragma once

#include <nlohmann/json.hpp>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

class ContractSchema
{
public:
	/// Checks the connection descriptor format and schema version.
	static void RequireClientABIFormat(const Json& Document);
	/// Returns the identity key for the accepted schema version.
	static const char* ContractIdentityKey(const Json& Document, const char* Key);
};
} // namespace Mod::SdkTool
