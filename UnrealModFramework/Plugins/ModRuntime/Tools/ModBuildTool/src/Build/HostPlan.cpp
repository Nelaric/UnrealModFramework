// Copyright (c) 2026 Nelaric Contributors
#include "HostPlan.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Archive/ArchiveInspector.h"
#include "ModBuildTool/src/Build/ArchiveWitness.h"
#include "ModBuildTool/src/Build/BuildInputs.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Cli/Version.h"

#include <exception>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Json HostPlan::MakePlan(const Request& Request)
{
	std::map<fs::path, std::string> Inputs = Request.Inputs;
	std::string Reason = "No valid successful receipt.";
	bool bUpToDate = false;
	if (fs::is_regular_file(Request.Receipt))
	{
		try
		{
			const Json Receipt = ContextDocument::ReadJson(Request.Receipt);
			ContextDocument::RequireFormat(Receipt, "UMF.ModHostReceipt", {2});
			if (Receipt.at("contextHash") != Request.ContextHash ||
			    Receipt.at("generationId") != Request.GenerationId ||
			    Receipt.at("toolVersion").get<std::string>() != Version::ToolVersion ||
			    Receipt.at("ownerModule") != Request.Owner ||
			    Receipt.at("library") != Path::PathToUtf8(Request.Library) ||
			    Receipt.at("target") != Request.Context.at("target") ||
			    Receipt.at("requiredSymbols") != Request.RequiredSymbols ||
			    Receipt.at("publicArtifacts") != Request.PublicArtifacts ||
			    Receipt.at("owningBinary") != Request.OwningBinary ||
			    Receipt.at("contractHash") != Request.Manifest.at("contractHash") ||
			    Receipt.at("projectionHash") != Request.Manifest.at("projectionHash") ||
			    Receipt.at("nativeABIHash") != Request.NativeABIHash)
			{
				Reason = "Build identity changed.";
			}
			else if (Receipt.value("environmentHash", "") != Request.EnvironmentHash)
			{
				Reason = "Effective compiler environment changed.";
			}
			else
			{
				for (const Json& File : Receipt.at("compilerDependencies"))
				{
					const fs::path Path =
					    Path::ResolvePath(Request.WorkingDirectory, ContextDocument::Text(File, "path"));
					Inputs[Path] = fs::is_regular_file(Path) ? Hash::HashFile(Path) : "missing";
				}
				if (Receipt.at("inputFingerprint") != BuildInputs::Fingerprint(Inputs))
				{
					Reason = "Compilation inputs changed or disappeared.";
				}
				else if (!fs::is_regular_file(Request.Library) ||
				         Receipt.at("libraryHash") != Hash::HashFile(Request.Library))
				{
					Reason = "Host archive is missing or its hash changed.";
				}
				else
				{
					ArchiveInspector::ValidateHostArchive(
					    Request.Library, Request.Platform, Request.Architecture,
					    ArchiveWitness::ArchiveSymbols(Request, Receipt.value("linkWitness", "")));
					bUpToDate = true;
					Reason = "All recorded inputs and the host archive match.";
				}
			}
		}
		catch (const std::exception& Error)
		{
			Reason = std::string("Invalid cached output: ") + Error.what();
		}
	}
	return {{"format", "UMF.ModHostPlan"},
	        {"schemaVersion", 2},
	        {"toolVersion", Version::ToolVersion},
	        {"ownerModule", Request.Owner},
	        {"target", Request.Context.at("target")},
	        {"contextHash", Request.ContextHash},
	        {"generationId", Request.GenerationId},
	        {"inputFingerprint", BuildInputs::Fingerprint(Inputs)},
	        {"environmentHash", Request.EnvironmentHash},
	        {"library", Path::PathToUtf8(Request.Library)},
	        {"receipt", Path::PathToUtf8(Request.Receipt)},
	        {"registrationAnchor", Request.Anchor},
	        {"manifestSchemaVersion", Request.Manifest.at("schemaVersion")},
	        {"requiredSymbols", Request.RequiredSymbols},
	        {"requiredBindings", Request.RequiredBindings},
	        {"publicArtifacts", Request.PublicArtifacts},
	        {"owningBinary", Request.OwningBinary},
	        {"contractHash", Request.Manifest.at("contractHash")},
	        {"projectionHash", Request.Manifest.at("projectionHash")},
	        {"nativeABIHash", Request.NativeABIHash},
	        {"compilationUnitCount", Request.Sources.size()},
	        {"upToDate", bUpToDate},
	        {"reason", Reason},
	        {"linkRequirement",
	         {{"kind", "OwnerBinaryOnly"},
	          {"ownerModule", Request.Owner},
	          {"owningBinary", Request.OwningBinary},
	          {"library", Path::PathToUtf8(Request.Library)},
	          {"retainSymbols", Json::array({Request.Anchor})},
	          {"producesImportLibrary", false}}}};
}

Json HostPlan::CurrentHostPlan(const Request& Request)
{
	if (Request.Manifest.at("schemaVersion") == 1)
	{
		throw std::runtime_error("finalize-host requires a generated-artifact manifest at version 2 or later.");
	}
	if (Request.OwningBinary.is_null())
	{
		throw std::runtime_error("finalize-host requires owningBinary in the host context.");
	}
	const Json Plan = MakePlan(Request);
	if (!Plan.at("upToDate").get<bool>())
	{
		throw std::runtime_error("Cannot finalize a stale host build: " + ContextDocument::Text(Plan, "reason"));
	}
	return Plan;
}
} // namespace Mod::BuildTool
