// Copyright (c) 2026 Nelaric Contributors
#include "ClientLayout.h"

#include "Common/Files/Path.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"

#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

std::string ClientLayout::ClientRelativePath(const Library& Job)
{
	const Json& Target = Job.Plan.at("target");
	return JsonDocument::Text(Target, "platform") + "/" + JsonDocument::Text(Target, "architecture") + "/" +
	       JsonDocument::Text(Target, "configuration") + "/" + JsonDocument::Text(Target, "type") + "/" +
	       JsonDocument::Text(Job.Plan, "ownerModule") +
	       Mod::BuildTool::Path::PathToUtf8(
	           Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Job.Plan, "library")).extension());
}
} // namespace Mod::SdkTool
