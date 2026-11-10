// Copyright (c) 2026 Nelaric Contributors
#include "HostContext.h"

#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Build/ContextDocument.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string HostContext::HashHostContext(const fs::path& Context)
{
	const Json Document = ContextDocument::ReadJson(Context);
	ContextDocument::RequireFormat(Document, "UMF.ModHostContext", {2});
	return Hash::HashBytes(Document.dump());
}
} // namespace Mod::BuildTool
