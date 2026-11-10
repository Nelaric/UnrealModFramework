// Copyright (c) 2026 Nelaric Contributors
#include "SdkInputs.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void SdkInputs::AddInput(SdkRequest& Request, const fs::path& File)
{
	const std::string Path = Mod::BuildTool::Path::PathToUtf8(fs::canonical(File));
	for (const Json& Input : Request.Inputs)
	{
		if (Input.at("path") == Path)
		{
			return;
		}
	}
	Request.Inputs.push_back({{"path", Path}, {"contentHash", Mod::BuildTool::Hash::HashFile(File)}});
}
} // namespace Mod::SdkTool
