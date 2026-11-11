// Copyright (c) 2026 Nelaric Contributors
#include "HostBuild.h"

#include "Common/Files/BuildLock.h"
#include "Common/Files/File.h"
#include "Common/Files/TemporaryName.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Build/ArchiveWitness.h"
#include "ModBuildTool/src/Build/BuildInputs.h"
#include "ModBuildTool/src/Build/CompiledObjects.h"
#include "ModBuildTool/src/Build/CompilerDependencies.h"
#include "ModBuildTool/src/Build/HostCompiler.h"
#include "ModBuildTool/src/Build/HostPlan.h"
#include "ModBuildTool/src/Build/HostPublisher.h"
#include "ModBuildTool/src/Build/LinkReceiptVerifier.h"
#include "ModBuildTool/src/Build/OwnedStage.h"
#include "ModBuildTool/src/Build/Request.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

HostBuild::HostBuild(fs::path ContextFile, fs::path ManifestFile)
    : Context(std::move(ContextFile)), Manifest(std::move(ManifestFile))
{
}

Json HostBuild::Plan() const
{
	return HostPlan::MakePlan(BuildInputs::ReadRequest(Context, Manifest));
}

Json HostBuild::Build(std::string_view ExpectedInput) const
{
	const BuildLock Generation(fs::canonical(Manifest).parent_path() / ".mht.lock", "");
	Request BuildRequest = BuildInputs::ReadRequest(Context, Manifest);
	BuildLock Lock(BuildRequest.Library);
	const Json Plan = HostPlan::MakePlan(BuildRequest);
	if (!ExpectedInput.empty() && ExpectedInput != Plan.at("inputFingerprint").get<std::string>())
	{
		throw std::runtime_error("InputsChanged: this MBT action no longer matches its planned input fingerprint.");
	}
	if (Plan.at("upToDate").get<bool>())
	{
		Json Result = Plan;
		Result["status"] = "UpToDate";
		return Result;
	}
	const fs::path Work = BuildRequest.Library.parent_path() / (".mbt-stage-" + TemporaryName::UniqueSuffix());
	if (!fs::create_directory(Work))
	{
		throw std::runtime_error("Cannot create a fresh owned build stage.");
	}
	OwnedStage Stage{Work, BuildRequest.Library.parent_path()};
	const std::string LinkWitness =
	    BuildRequest.bPeLinkWitness
	        ? std::string(ArchiveWitness::LinkWitnessPrefix) +
	              Hash::HashBytes(BuildRequest.ContextHash + BuildRequest.GenerationId + TemporaryName::UniqueSuffix())
	        : std::string();
	const auto Started = fs::file_time_type::clock::now();
	const auto Objects = HostCompiler::CompileObjects(BuildRequest, Work, LinkWitness, Started);
	const auto Archive = HostCompiler::CreateHostArchive(BuildRequest, Work, Objects.Paths, LinkWitness);
	const auto Dependencies = CompilerDependencies::HashCompilerDependencies(Objects.Dependencies, Started);
	const Request Current = BuildInputs::ReadRequest(Context, Manifest);
	if (Current.Inputs != BuildRequest.Inputs)
	{
		throw std::runtime_error("InputsChanged: build inputs changed before publication.");
	}
	for (const auto& [Path, Digest] : Dependencies)
	{
		BuildRequest.Inputs[Path] = Digest;
	}
	HostPublisher::PublishHostArchive(BuildRequest, Archive, LinkWitness, Dependencies);
	Json Result = HostPlan::MakePlan(BuildRequest);
	if (!Result.at("upToDate").get<bool>())
	{
		throw std::runtime_error("Published archive failed receipt verification.");
	}
	Result["status"] = "Built";
	return Result;
}

Json HostBuild::Finalize(const fs::path& LinkReceipt) const
{
	const BuildLock Generation(fs::canonical(Manifest).parent_path() / ".mht.lock", "");
	const Request Request = BuildInputs::ReadRequest(Context, Manifest);
	const BuildLock Library(Request.Library);
	const Json Plan = HostPlan::CurrentHostPlan(Request);
	const fs::path LinkPath = fs::canonical(LinkReceipt);
	const std::string Bytes = File::ReadFile(LinkPath);
	const std::string LinkHash = Hash::HashBytes(Bytes);
	Json Result = LinkReceiptVerifier::VerifyLink(Request, Plan, LinkPath, Json::parse(Bytes), LinkHash);
	if (Hash::HashFile(LinkPath) != LinkHash)
	{
		throw std::runtime_error("InputsChanged: the link record changed during finalization.");
	}
	return Result;
}

} // namespace Mod::BuildTool
