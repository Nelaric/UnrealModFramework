// Copyright (c) 2026 Nelaric Contributors
#include "ContractSchema.h"

#include "ModSDKTool/src/Manifest/JsonDocument.h"

#include <nlohmann/json.hpp>
#include <string_view>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

void ContractSchema::RequireClientABIFormat(const Json& Document)
{
	const bool bLegacy = Document.contains("schemaVersion") && Document.at("schemaVersion") == 1;
	JsonDocument::RequireFormat(Document,
	                            bLegacy ? "UMF.ClientA"
	                                      "bi"
	                                    : "UMF.ClientABI",
	                            {1, 2});
}

const char* ContractSchema::ContractIdentityKey(const Json& Document, const char* Key)
{
	if (std::string_view(Key) == "nativeABIHash")
	{
		const int FirstVersion = Document.at("format") == "UMF.GeneratedArtifacts" ? 3 : 2;
		if (Document.at("schemaVersion") < FirstVersion)
		{
			return "nativeA"
			       "biHash";
		}
	}
	return Key;
}
} // namespace Mod::SdkTool
