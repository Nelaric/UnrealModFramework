// Copyright (c) 2026 Nelaric Contributors
#include "HostPublisher.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Build/BuildInputs.h"
#include "ModBuildTool/src/Cli/Version.h"

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void HostPublisher::PublishHostArchive(const Request& Request, const fs::path& Archive, const std::string& LinkWitness,
                                       const std::map<fs::path, std::string>& Dependencies)
{
	const Json Receipt = {{"format", "UMF.ModHostReceipt"},
	                      {"schemaVersion", 2},
	                      {"toolVersion", Version::ToolVersion},
	                      {"ownerModule", Request.Owner},
	                      {"target", Request.Context.at("target")},
	                      {"contextHash", Request.ContextHash},
	                      {"generationId", Request.GenerationId},
	                      {"inputFingerprint", BuildInputs::Fingerprint(Request.Inputs)},
	                      {"environmentHash", Request.EnvironmentHash},
	                      {"library", Path::PathToUtf8(Request.Library)},
	                      {"libraryHash", Hash::HashFile(Archive)},
	                      {"linkWitness", LinkWitness},
	                      {"registrationAnchor", Request.Anchor},
	                      {"requiredSymbols", Request.RequiredSymbols},
	                      {"publicArtifacts", Request.PublicArtifacts},
	                      {"owningBinary", Request.OwningBinary},
	                      {"contractHash", Request.Manifest.at("contractHash")},
	                      {"projectionHash", Request.Manifest.at("projectionHash")},
	                      {"nativeABIHash", Request.NativeABIHash},
	                      {"compilerDependencies", BuildInputs::InputList(Dependencies)}};
	// The receipt is the commit record. A partial publish never validates.
	File::ReplaceFileAtomically(Archive, Request.Library);
	File::WriteFileAtomically(Request.Receipt, Receipt.dump(2) + "\n");
}
} // namespace Mod::BuildTool
