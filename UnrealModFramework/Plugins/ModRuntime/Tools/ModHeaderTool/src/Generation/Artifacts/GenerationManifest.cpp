// Copyright (c) 2026 Nelaric Contributors
#include "GenerationManifest.h"

#include "Common/Files/WatchedFiles.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/Artifacts/GenerationPaths.h"
#include "ModHeaderTool/src/Generation/Artifacts/LifecycleExport.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Hash/Hash.h"
#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Project/ProjectSources.h"

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Files = std::map<std::string, std::string>;

InputIndex GenerationManifest::DescribeInputs(const GenerationModel& Model)
{
	InputIndex Index;
	for (const auto& File : Model.Dependencies)
	{
		const auto Digest = fs::exists(File) ? Hash::HashFile(File) : std::string();
		if (File == Model.SourceErrorCatalog && Digest != Model.SourceErrorCatalogHash)
		{
			throw std::runtime_error("The source error catalog changed during generation: " +
			                         DiagnosticLocation::QuoteDiagnosticPath(File));
		}
		const auto Leaf = Path::PathToUtf8(File.filename());
		const bool ContextFile = File.extension() == ".rsp" || Leaf.starts_with("Definitions.") ||
		                         Leaf.starts_with("SharedDefinitions.") || Leaf.starts_with("SharedPCH.");
		Index.Dependencies.push_back(
		    {{"path", GenerationPaths::NormalizeInput(Model.Input, Path::PathToUtf8(File))}, {"contentHash", Digest}});
		Index.Files.push_back({{"path", Path::PathToUtf8(File)},
		                       {"contentHash", Digest},
		                       {"kind", ContextFile ? "AnalysisContext" : "Semantic"}});
	}
	return Index;
}

OutputIndex GenerationManifest::DescribeOutputs(const Files& Files, const std::string& ErrorHeader)
{
	OutputIndex Index;
	for (const auto& [Path, Bytes] : Files)
	{
		const bool Host = Path.starts_with("Host/");
		const bool Implementation = Path.ends_with(".cpp");
		std::string Owner;
		if (Host)
		{
			Owner = Path.substr(5, Path.find('/', 5) - 5);
		}
		else if (Path.starts_with("Client/"))
		{
			Owner = Path.substr(7, Path.find('/', 7) - 7);
		}
		else if (Implementation)
		{
			Owner = Path.substr(12, Path.find(".modviews.cpp") - 12);
		}
		const auto Digest = Hash::HashBytes(Bytes);
		Index.Hashes += Path + ":" + Digest + "\n";
		Index.Generated.push_back(
		    {{"path", Path},
		     {"ownerModule", Owner.empty() ? Json(nullptr) : Json(Owner)},
		     {"role", Implementation ? (Path.ends_with(".modverify.cpp") ? "CompileVerification" : "HostBinding")
		              : Host         ? "HostHeader"
		              : Path == LifecycleExport::LifecyclePath ? "SdkResource"
		              : (Path.ends_with(".modabi.h") || Path.ends_with(".modextensionsabi.h") ||
		                 Path == "SDK/Public/ModExtensionABI.h" || Path == ErrorHeader)
		                  ? "SdkHeader"
		              : Path.starts_with("SDK/")      ? "HostHeader"
		              : Path.starts_with("Contract/") ? "Contract"
		                                              : "Diagnostic"},
		     {"contentHash", Digest}});
		if (Implementation)
		{
			Index.CompilationUnits.push_back({{"ownerModule", Owner}, {"source", Path}});
		}
	}
	return Index;
}

Json GenerationManifest::ReadHostContextHashes(const GenerationModel& Model)
{
	Json HostContexts = Json::object();
	for (const auto& [Owner, File] : Model.Input.HostContexts)
	{
		const auto Context = Json::parse(TextFile::ReadTextFile(File));
		if (Context.value("format", "") != "UMF.ModHostContext" || Context.value("schemaVersion", 0) != 2 ||
		    Context.value("ownerModule", "") != Owner || Context.value("moduleAPIMode", "") != "Producer" ||
		    Context.at("target").value("name", "") != Model.Input.Target ||
		    Context.at("target").value("platform", "") != Model.Input.Platform ||
		    Context.at("target").value("architecture", "") != Model.Input.Architecture ||
		    Context.at("target").value("configuration", "") != Model.Input.Configuration)
		{
			throw std::runtime_error("MBT host context does not match this module and Target: " + Owner);
		}
		HostContexts[Owner] = Hash::HashBytes(Context.dump());
	}
	return HostContexts;
}

Json GenerationManifest::DescribeWatches(const GenerationModel& Model)
{
	Json ConfigWatches = Json::array();
	for (const auto& Root : Model.ConfigRoots)
	{
		std::set<std::string> Names;
		for (const auto& File : Tools::WatchedFiles::CollectConfigFiles(Root))
		{
			Names.insert(Path::PathToUtf8(File));
		}
		ConfigWatches.push_back({{"root", Path::PathToUtf8(Root)}, {"kind", "Config"}, {"files", Names}});
	}
	for (const auto& Module : Model.Input.Modules)
	{
		Project Scoped = Model.Input.Project;
		Scoped.Directories = {Module.Root};
		std::set<std::string> Names;
		for (const auto& File : ProjectSources::CollectSourceFiles(Scoped))
		{
			Names.insert(Path::PathToUtf8(File));
		}
		ConfigWatches.push_back({{"root", Path::PathToUtf8(Module.Root)}, {"kind", "Source"}, {"files", Names}});
	}
	return ConfigWatches;
}

Json GenerationManifest::MakeManifest(const GenerationModel& Model, const GeneratedBatch& Batch)
{
	const auto Inputs = DescribeInputs(Model);
	const auto ContextHash = Hash::HashBytes(Json{
	    {"target", Model.Input.Target},
	    {"platform", Model.Input.Platform},
	    {"configuration", Model.Input.Configuration},
	    {"architecture", Model.Input.Architecture},
	    {"commands", Model.Commands},
	    {"clang", Model.ClangVersion},
	    {"macros", Model.ContextMacros}}.dump());
	const auto InputHash = Hash::HashBytes(Inputs.Dependencies.dump() + ContextHash);
	const auto Outputs = DescribeOutputs(Batch.Files, Batch.ErrorHeader);
	const auto GenerationId = Hash::HashBytes(InputHash + Batch.ContractHash + GeneratorVersion + Outputs.Hashes);
	const auto HostContexts = ReadHostContextHashes(Model);
	const auto ConfigWatches = DescribeWatches(Model);
	return {{"schemaVersion", 3},
	        {"generatorVersion", GeneratorVersion},
	        {"format", "UMF.GeneratedArtifacts"},
	        {"outputLayout", "Flat"},
	        {"backend", "UObjectAPI.Win64.v1"},
	        {"gameId", Model.Input.GameId},
	        {"target", Model.Input.Target},
	        {"platform", Model.Input.Platform},
	        {"configuration", Model.Input.Configuration},
	        {"architecture", Model.Input.Architecture},
	        {"generationId", GenerationId},
	        {"status", "Complete"},
	        {"inputHash", InputHash},
	        {"buildContextHash", ContextHash},
	        {"contractHash", Batch.ContractHash},
	        {"projectionHash", Batch.ProjectionHash},
	        {"nativeABIHash", Batch.NativeABI.at("nativeABIHash")},
	        {"independentNativeABI", true},
	        {"clientABI", Batch.NativeABI},
	        {"requiredSymbols", Batch.NativeABI.at("requiredSymbols")},
	        {"unresolvedTypeCount", 0},
	        {"generatedFiles", Outputs.Generated},
	        {"compilationUnits", Outputs.CompilationUnits},
	        {"hostBuildContexts", HostContexts},
	        {"dependencies", Inputs.Dependencies},
	        {"inputFiles", Inputs.Files},
	        {"watchedDirectories", ConfigWatches}};
}
} // namespace Mod::HeaderTool
