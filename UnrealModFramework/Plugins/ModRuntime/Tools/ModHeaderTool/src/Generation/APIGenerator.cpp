// Copyright (c) 2026 Nelaric Contributors
#include "APIGenerator.h"

#include "ModHeaderTool/src/Generation/BlueprintExports.h"
#include "ModHeaderTool/src/Analysis/DeclarationAnalyzer.h"
#include "ModHeaderTool/src/Analysis/SourceErrorAnalyzer.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Generation/Artifacts/ArtifactEmitter.h"
#include "ModHeaderTool/src/Generation/Artifacts/LifecycleExport.h"
#include "ModHeaderTool/src/Generation/GenerationInputReader.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"
#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Hash/Hash.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

APIGenerator::APIGenerator(const fs::path& BuildInput, const fs::path& HostContexts)
    : Input(GenerationInputReader::LoadGenerationInput(BuildInput))
{
	if (HostContexts.empty())
	{
		throw std::runtime_error("Complete generation requires a context set for all Host and Client producers.");
	}
	ContextFile = fs::canonical(HostContexts);
	ContextSet = Json::parse(TextFile::ReadTextFile(ContextFile));
	if (ContextSet.value("format", "") != "UMF.ModHostContextSet" || ContextSet.value("schemaVersion", 0) != 1)
	{
		throw std::runtime_error("Unsupported MBT host context set.");
	}
	std::set<std::string> Owners{Input.GameId + "ModClient"};
	for (const auto& Module : Input.Modules)
	{
		if (!Owners.insert(Module.Name).second)
		{
			throw std::runtime_error("The reserved Client producer conflicts with host module: " + Module.Name);
		}
	}
	const auto& Contexts = ContextSet.at("contexts");
	if (!Contexts.is_object())
	{
		throw std::runtime_error("MBT context set requires a contexts object keyed by owner.");
	}
	for (const auto& Owner : Owners)
	{
		if (!Contexts.contains(Owner))
		{
			throw std::runtime_error("MBT context set is missing producer: " + Owner);
		}
		Input.HostContexts.emplace(Owner, fs::canonical(ContextFile.parent_path() /
		                                                Path::PathFromUtf8(Contexts.at(Owner).get<std::string>())));
	}
	if (Contexts.size() != Owners.size())
	{
		throw std::runtime_error("MBT context set contains producers outside this generation.");
	}
}

fs::path APIGenerator::ResolveAnalysisDatabase() const
{
	// The UE adapter pins expanded arguments before UBT rewrites its response files.
	fs::path AnalysisDatabase = Input.Database;
	if (ContextSet.contains("analysisCompilationDatabase"))
	{
		const auto ReadPath = [&](const char* Key) {
			return fs::canonical(ContextFile.parent_path() / Path::PathFromUtf8(ContextSet.at(Key).get<std::string>()));
		};
		if (!fs::equivalent(Input.Database, ReadPath("sourceCompilationDatabase")))
		{
			throw std::runtime_error("The analysis snapshot belongs to a different compilation database.");
		}
		AnalysisDatabase = ReadPath("analysisCompilationDatabase");
		if (ContextSet.value("sourceCompilationDatabaseHash", "") != Hash::HashFile(Input.Database) ||
		    ContextSet.value("analysisCompilationDatabaseHash", "") != Hash::HashFile(AnalysisDatabase))
		{
			throw std::runtime_error("The pinned compilation database changed. Refresh the producer context set.");
		}
	}
	return AnalysisDatabase;
}

void APIGenerator::Generate(const fs::path& OutputDirectory, const fs::path& Options, const fs::path& ClangLibrary,
                            const fs::path& LifecycleSource, const fs::path& BlueprintSnapshot) &&
{
	const fs::path AnalysisDatabase = ResolveAnalysisDatabase();
	auto Model = DeclarationAnalyzer::Analyze(std::move(Input), Options, ClangLibrary, AnalysisDatabase);
	const auto SourceToolPath = ContextSet.value("sourceTool", "");
	SourceErrorAnalyzer::AnalyzeSourceErrors(
	    Model, AnalysisDatabase, ClangLibrary,
	    SourceToolPath.empty() ? fs::path{} : ContextFile.parent_path() / Path::PathFromUtf8(SourceToolPath));
	BlueprintExports::Merge(Model, BlueprintSnapshot);
	if (!LifecycleSource.empty())
	{
		Model.LifecycleSource = fs::canonical(LifecycleSource);
		for (const std::string Name : LifecycleExport::LifecycleClasses)
		{
			Model.Dependencies.insert(fs::canonical(Model.LifecycleSource / (Name + ".h")));
		}
	}
	Model.Dependencies.insert(ContextFile);
	for (const auto& [Owner, File] : Model.Input.HostContexts)
	{
		Model.Dependencies.insert(File);
	}
	ArtifactEmitter::Emit(std::move(Model), OutputDirectory);
}
} // namespace Mod::HeaderTool
