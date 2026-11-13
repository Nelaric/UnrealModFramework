// Copyright (c) 2026 Nelaric Contributors
#include "SdkBuild.h"

#include "Common/Files/BuildLock.h"
#include "Common/Files/Executable.h"
#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"
#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Build/BuildProgress.h"
#include "ModSDKTool/src/Build/NativeBuild.h"
#include "ModSDKTool/src/Build/PathRelations.h"
#include "ModSDKTool/src/Build/SdkInputCheck.h"
#include "ModSDKTool/src/Build/Workspace.h"
#include "ModSDKTool/src/Bundle/BundleAssembly.h"
#include "ModSDKTool/src/Bundle/BundlePublisher.h"
#include "ModSDKTool/src/Bundle/BundleReuse.h"
#include "ModSDKTool/src/Editor/EditorPackage.h"
#include "ModSDKTool/src/Editor/PluginSources.h"
#include "ModSDKTool/src/Manifest/ContractSchema.h"
#include "ModSDKTool/src/Manifest/ContractValidator.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/Library.h"
#include "ModSDKTool/src/Manifest/PortableNames.h"
#include "ModSDKTool/src/Manifest/SdkInputs.h"
#include "ModSDKTool/src/Manifest/SdkPlan.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Mod::BuildTool::BuildLock;

void SdkBuild::ValidatePublishInputs() const
{
	using namespace Mod::BuildTool;
	SdkInputCheck::CheckInputs(Request);
	const std::string ExpectedFingerprint = Request.Fingerprint;
	SdkBuild Current(Request.File);
	Current.Plan();
	if (Current.Request.Fingerprint != ExpectedFingerprint ||
	    std::any_of(Current.Request.Libraries.begin(), Current.Request.Libraries.end(),
	                [](const Library& Job) { return !Job.Plan.at("upToDate").get<bool>(); }))
	{
		throw std::runtime_error("InputsChanged: native compilation dependencies changed during SDK assembly.");
	}
	for (const Library& Job : Request.Libraries)
	{
		const fs::path Receipt = Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Job.Plan, "receipt"));
		if (Mod::BuildTool::Hash::HashFile(Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(
		        Job.Plan, "library"))) != JsonDocument::Text(JsonDocument::ReadJson(Receipt), "libraryHash"))
		{
			throw std::runtime_error("Native library changed during UE SDK compilation.");
		}
	}
}

Json SdkBuild::Build()
{
	using namespace Mod::BuildTool;
	Json PlanResult = Plan();
	BuildLock Lock(
	    Request.Output.parent_path() /
	    Mod::BuildTool::Path::PathFromUtf8("." + Mod::BuildTool::Path::PathToUtf8(Request.Output.filename()) + ".mst"));
	if (fs::exists(Request.Output))
	{
		return BundleReuse::ReuseBundle(Request);
	}
	const Workspace Work(Request.Document.contains("workDirectory")
	                         ? Mod::BuildTool::Path::ResolvePath(Request.File.parent_path(),
	                                                             JsonDocument::Text(Request.Document, "workDirectory"))
	                         : fs::temp_directory_path());
	const auto LibraryResults = NativeBuild::BuildNativeLibraries(Request, Work.Root);
	// Newly discovered compiler dependencies participate in the final SDK key.
	PlanResult = Plan();
	SdkInputCheck::CheckInputs(Request);
	const std::string Plugin = JsonDocument::Text(Request.Document, "pluginName");
	const auto Packages = EditorPackage::BuildSdkPackages(Request, Work.Root, Plugin);
	const fs::path Bundle = Work.Root / "Bundle";
	BundleAssembly::CopySdkPackages(Request, Packages, Plugin, Bundle);
	Mod::BuildTool::File::WriteFileAtomically(Bundle / "Build" / "Plan.json", PlanResult.dump(2) + "\n");
	Mod::BuildTool::File::WriteFileAtomically(Bundle / "Build" / "NativeResults.json", LibraryResults.dump(2) + "\n");
	BundleAssembly::CopySdkNativeArtifacts(Request, Bundle);
	const auto Capabilities = BundleAssembly::WriteBundleManifest(Request, Bundle, Plugin);
	ValidatePublishInputs();
	BuildProgress::ReportStage("Publish", "Publishing verified authoring artifacts; game binding is not asserted.");
	BundlePublisher::Publish(Request, Bundle);
	return {{"format", "UMF.SdkResult"},
	        {"schemaVersion", 1},
	        {"status", "Built"},
	        {"bundle", Mod::BuildTool::Path::PathToUtf8(Request.Output)},
	        {"inputFingerprint", Request.Fingerprint},
	        {"capabilities", Capabilities}};
}

SdkBuild::SdkBuild(const fs::path& File) : Request(ReadRequest(File))
{
}

SdkRequest SdkBuild::ReadRequest(const fs::path& File)
{
	using namespace Mod::BuildTool;
	SdkRequest Request;
	Request.File = fs::canonical(File);
	Request.Document = JsonDocument::ReadJson(Request.File);
	JsonDocument::RequireFormat(Request.Document, "UMF.SdkBuildRequest", {2});
	const fs::path Root = Request.File.parent_path();
	const Json& Document = Request.Document;
	if (Document.contains("developmentPlugin"))
	{
		throw std::runtime_error("Remove developmentPlugin from the request. MDT generates the authoring plugin.");
	}
	JsonDocument::Text(Document, "gameId");
	const std::string Plugin = JsonDocument::Text(Document, "pluginName");
	PortableNames::Identifier(Plugin);
	if (PortableNames::Fold(Plugin) == PortableNames::Fold(PluginSources::DevelopmentPluginName))
	{
		throw std::runtime_error("The generated SDK and independent development plugin need distinct names.");
	}
	const std::string Version = JsonDocument::Text(Document, "sdkVersion");
	if (Version.find_first_not_of("0123456789.") != std::string::npos || Version.front() == '.' ||
	    Version.back() == '.' || std::count(Version.begin(), Version.end(), '.') != 2 ||
	    Version.find("..") != std::string::npos)
	{
		throw std::runtime_error("sdkVersion must contain three numeric components.");
	}
	Request.Project = fs::canonical(Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Document, "project")));
	if (Request.Project.extension() != ".uproject")
	{
		throw std::runtime_error("project must identify a real .uproject file.");
	}
	JsonDocument::ReadJson(Request.Project);
	Request.Output =
	    Document.contains("outputDirectory")
	        ? Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Document, "outputDirectory"))
	        : Mod::BuildTool::Path::ResolvePath(Request.Project.parent_path(),
	                                            "Intermediate/ModTools/ModSDKTool/" + Plugin + "/" + Version);
	while (Request.Output.has_relative_path() && !Request.Output.has_filename())
	{
		Request.Output = Request.Output.parent_path();
	}
	Request.ContractFile =
	    fs::canonical(Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Document, "contract")));
	Request.Contract = JsonDocument::ReadJson(Request.ContractFile);
	ContractValidator::ValidateContract(Request);
	Request.Mbt =
	    fs::canonical(Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Document.at("tools"), "mbt")));
	Request.Dotnet =
	    fs::canonical(Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Document.at("tools"), "dotnet")));
	Request.AutomationTool = fs::canonical(
	    Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Document.at("tools"), "automationTool")));
	const Json& Editor = Document.at("editor");
	const std::string Platform = JsonDocument::Text(Editor, "platform");
	const std::string Architecture = JsonDocument::Text(Editor, "architecture");
	if (
#ifdef _WIN32
	    Platform != "Win64"
#elif defined(__APPLE__)
	    Platform != "Mac"
#else
	    Platform != "Linux"
#endif
	    || (Architecture != "x64" && Architecture != "arm64") || Editor.value("configuration", "") != "Development")
	{
		throw std::runtime_error("Author Editor builds require the current host platform and Development.");
	}
	const fs::path Modules =
	    fs::canonical(Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Editor, "modules")));
	if (JsonDocument::Text(JsonDocument::ReadJson(Modules), "BuildId") != JsonDocument::Text(Editor, "buildId"))
	{
		throw std::runtime_error("Author Editor BuildId does not match its real engine module manifest.");
	}
	if (Document.contains("timeoutSeconds") &&
	    (!Document.at("timeoutSeconds").is_number_integer() || Document.at("timeoutSeconds") < 1 ||
	     Document.at("timeoutSeconds") > 86400))
	{
		throw std::runtime_error("timeoutSeconds must be between 1 and 86400.");
	}
	for (const fs::path& Input : {Request.File, Request.Project, Request.ContractFile, Request.Mbt, Request.Dotnet,
	                              Request.AutomationTool, Modules, Mod::BuildTool::Executable::ExecutablePath()})
	{
		SdkInputs::AddInput(Request, Input);
	}
	for (const char* Name : {"UnrealModFramework.LICENSE", "nlohmann.LICENSE.MIT", "NOTICE.md"})
	{
		SdkInputs::AddInput(Request, Mod::BuildTool::Executable::ExecutablePath().parent_path() / "Licenses" / Name);
	}
	const Json& Libraries = Document.at("libraries");
	if (!Libraries.is_array() || Libraries.empty() || Libraries.size() > 256)
	{
		throw std::runtime_error("libraries must be a bounded nonempty array.");
	}
	for (const Json& Entry : Libraries)
	{
		Library Job;
		Job.Role = JsonDocument::Text(Entry, "role");
		if (Job.Role != "Host" && Job.Role != "Client")
		{
			throw std::runtime_error("Library role must be Host or Client.");
		}
		Job.Context = fs::canonical(Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Entry, "context")));
		Job.Manifest =
		    fs::canonical(Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Entry, "generatedManifest")));
		const Json Context = JsonDocument::ReadJson(Job.Context);
		const Json Manifest = JsonDocument::ReadJson(Job.Manifest);
		JsonDocument::RequireFormat(Manifest, "UMF.GeneratedArtifacts", {1, 2, 3});
		if (Manifest.value("status", "") != "Complete")
		{
			throw std::runtime_error("MST needs complete generated artifacts; MHT scan reports are not SDK inputs.");
		}
		for (const char* Key : {"contractHash", "projectionHash", "nativeABIHash"})
		{
			if (Manifest.at(ContractSchema::ContractIdentityKey(Manifest, Key)) !=
			    Request.Contract.at(ContractSchema::ContractIdentityKey(Request.Contract, Key)))
			{
				throw std::runtime_error(std::string("Mixed SDK batches: ") + Key);
			}
		}
		SdkInputs::AddInput(Request, Job.Context);
		SdkInputs::AddInput(Request, Job.Manifest);
		for (const Json& Generated : Manifest.at("generatedFiles"))
		{
			const fs::path Path = Mod::BuildTool::Path::ResolveGeneratedPath(Job.Manifest.parent_path(),
			                                                                 JsonDocument::Text(Generated, "path"));
			SdkInputs::AddInput(Request, Path);
		}
		const fs::path LibraryPath =
		    Mod::BuildTool::Path::ResolvePath(Job.Context.parent_path(), JsonDocument::Text(Context, "outputLibrary"));
		if (PathRelations::IsInside(LibraryPath, Request.Output) ||
		    PathRelations::IsInside(Request.Output, Job.Manifest.parent_path()) ||
		    PathRelations::IsInside(Request.Output, LibraryPath))
		{
			throw std::runtime_error("SDK output overlaps a library or generated input root.");
		}
		Request.Libraries.push_back(std::move(Job));
	}
	for (const Json& Input : Request.Inputs)
	{
		if (PathRelations::IsInside(Mod::BuildTool::Path::PathFromUtf8(Input.at("path").get<std::string>()),
		                            Request.Output))
		{
			throw std::runtime_error("SDK output would contain or replace an input.");
		}
	}
	if (Document.contains("workDirectory"))
	{
		const fs::path WorkRoot =
		    Mod::BuildTool::Path::ResolvePath(Root, JsonDocument::Text(Document, "workDirectory"));
		if (PathRelations::IsInside(WorkRoot, Request.Output))
		{
			throw std::runtime_error("workDirectory cannot be inside the SDK output.");
		}
		for (const Library& Job : Request.Libraries)
		{
			if (PathRelations::IsInside(WorkRoot, Job.Manifest.parent_path()))
			{
				throw std::runtime_error("workDirectory cannot be inside generated input roots.");
			}
		}
	}
	std::sort(Request.Inputs.begin(), Request.Inputs.end(), [](const Json& Left, const Json& Right)
	          { return Left.at("path").get<std::string>() < Right.at("path").get<std::string>(); });
	return Request;
}

void SdkBuild::ValidateResultOutput(const fs::path& Output) const
{
	if (Output.empty())
	{
		return;
	}
	const fs::path Destination = fs::weakly_canonical(fs::absolute(Output));
	if (PathRelations::IsInside(Destination, Request.Output))
	{
		throw std::runtime_error("Result output must be outside the SDK bundle.");
	}
	for (const Json& Input : Request.Inputs)
	{
		if (PathRelations::SamePath(Destination,
		                            Mod::BuildTool::Path::PathFromUtf8(Input.at("path").get<std::string>())))
		{
			throw std::runtime_error("Result output would replace an SDK input.");
		}
	}
	for (const Library& Job : Request.Libraries)
	{
		const Json Context = JsonDocument::ReadJson(Job.Context);
		const fs::path Library =
		    Mod::BuildTool::Path::ResolvePath(Job.Context.parent_path(), JsonDocument::Text(Context, "outputLibrary"));
		if (PathRelations::SamePath(Destination, Library) ||
		    PathRelations::SamePath(Destination, fs::path(Library.native() + fs::path(".mbt.json").native())))
		{
			throw std::runtime_error("Result output would replace a native artifact.");
		}
	}
}

Json SdkBuild::Plan()
{
	using namespace Mod::BuildTool;
	BuildProgress::ReportStage("Plan", "Validating complete contracts and native library inputs.");
	const fs::path Temporary =
	    fs::temp_directory_path() /
	    Mod::BuildTool::Path::PathFromUtf8("umf-mst-plan-" + Mod::BuildTool::TemporaryName::UniqueSuffix());
	if (!fs::create_directory(Temporary))
	{
		throw std::runtime_error("Cannot create an exclusive MST planning directory.");
	}
	try
	{
		const auto Jobs = SdkPlan::CollectLibraryPlans(Request, Temporary);
		Request.Fingerprint = SdkPlan::FingerprintPlan(Request, Jobs);
		fs::remove_all(Temporary);
		return SdkPlan::MakeSdkPlan(Request, Jobs);
	}
	catch (...)
	{
		std::error_code Ignored;
		fs::remove_all(Temporary, Ignored);
		throw;
	}
}
} // namespace Mod::SdkTool
