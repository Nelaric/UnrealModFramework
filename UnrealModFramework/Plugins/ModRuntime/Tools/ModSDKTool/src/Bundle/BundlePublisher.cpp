// Copyright (c) 2026 Nelaric Contributors
#include "BundlePublisher.h"

#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"
#include "ModSDKTool/src/Build/StageFiles.h"
#include "ModSDKTool/src/Bundle/SdkBundle.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

void BundlePublisher::Publish(const SdkRequest& Request, const fs::path& Stage)
{
	fs::create_directories(Request.Output.parent_path());
	// Keep the sibling stage short: UE precompiled outputs have deep paths.
	const fs::path Incoming =
	    Request.Output.parent_path() /
	    Mod::BuildTool::Path::PathFromUtf8(".mst-" + Mod::BuildTool::TemporaryName::UniqueSuffix());
	if (!fs::create_directory(Incoming))
	{
		throw std::runtime_error("Cannot create the bundle's sibling publication directory.");
	}
	try
	{
		StageFiles::CopyTree(Stage, Incoming);
		SdkBundle::InspectSdk(Incoming);
		if (fs::exists(Request.Output))
		{
			throw std::runtime_error("A published SDK is immutable; choose a new versioned output directory.");
		}
		fs::rename(Incoming, Request.Output);
	}
	catch (...)
	{
		std::error_code Ignored;
		fs::remove_all(Incoming, Ignored);
		throw;
	}
}
} // namespace Mod::SdkTool
