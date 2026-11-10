// Copyright (c) 2026 Nelaric Contributors
#include "SdkInputCheck.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

void SdkInputCheck::CheckInputs(const SdkRequest& Request)
{
	for (const Json& Input : Request.Inputs)
	{
		if (Mod::BuildTool::Hash::HashFile(Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Input, "path"))) !=
		    JsonDocument::Text(Input, "contentHash"))
		{
			throw std::runtime_error("InputsChanged: SDK input changed: " + JsonDocument::Text(Input, "path"));
		}
	}
}
} // namespace Mod::SdkTool
