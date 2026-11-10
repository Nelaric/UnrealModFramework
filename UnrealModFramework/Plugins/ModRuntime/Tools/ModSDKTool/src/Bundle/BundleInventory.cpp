// Copyright (c) 2026 Nelaric Contributors
#include "BundleInventory.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Json BundleInventory::Inventory(const fs::path& Root)
{
	Json Result = Json::array();
	std::set<std::string> Seen;
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Root))
	{
		if (Entry.is_symlink())
		{
			throw std::runtime_error("SDK bundles cannot contain symbolic links.");
		}
		if (Entry.is_regular_file() && Entry.path() != Root / "Manifest.json")
		{
			const std::string Relative = Mod::BuildTool::Path::PathToUtf8(Entry.path().lexically_relative(Root));
			if (!Seen.insert(Relative).second)
			{
				throw std::runtime_error("Duplicate bundle inventory path.");
			}
			Result.push_back({{"path", Relative}, {"contentHash", Mod::BuildTool::Hash::HashFile(Entry.path())}});
		}
	}
	std::sort(Result.begin(), Result.end(), [](const Json& Left, const Json& Right)
	          { return Left.at("path").get<std::string>() < Right.at("path").get<std::string>(); });
	return Result;
}
} // namespace Mod::SdkTool
