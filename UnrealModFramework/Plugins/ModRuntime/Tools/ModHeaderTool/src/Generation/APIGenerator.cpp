// Copyright (c) 2026 Nelaric Contributors
#include "APIGenerator.h"

#include "ModHeaderTool/src/Generation/BlueprintExports.h"
#include "ModHeaderTool/src/Analysis/DeclarationAnalyzer.h"
#include "ModHeaderTool/src/Analysis/SourceErrorAnalyzer.h"
#include "ModHeaderTool/src/Analysis/SourceToolInvocation.h"
#include "ModHeaderTool/src/Generation/Artifacts/ArtifactEmitter.h"
#include "ModHeaderTool/src/Generation/Artifacts/LifecycleExport.h"
#include "ModHeaderTool/src/Generation/GenerationInputReader.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

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

APIGenerator::APIGenerator(const fs::path& BuildInput) : Input(GenerationInputReader::LoadGenerationInput(BuildInput))
{
}

void APIGenerator::Generate(const fs::path& OutputDirectory, const fs::path& Options, const fs::path& ClangLibrary,
                            const fs::path& LifecycleSource, const fs::path& BlueprintSnapshot,
                            const fs::path& SourceTool) &&
{
	const fs::path AnalysisDatabase = Input.Database;
	auto Model = DeclarationAnalyzer::Analyze(std::move(Input), Options, ClangLibrary, AnalysisDatabase);
	SourceErrorAnalyzer::AnalyzeSourceErrors(Model, AnalysisDatabase, ClangLibrary, SourceTool);
	SourceToolInvocation::RewriteHooks(Model, AnalysisDatabase, ClangLibrary, SourceTool);
	BlueprintExports::Merge(Model, BlueprintSnapshot);
	if (!LifecycleSource.empty())
	{
		Model.LifecycleSource = fs::canonical(LifecycleSource);
		Model.Dependencies.insert(fs::canonical(Model.LifecycleSource / "ModExtensionABI.h"));
		for (const std::string Name : LifecycleExport::LifecycleClasses)
		{
			Model.Dependencies.insert(fs::canonical(Model.LifecycleSource / (Name + ".h")));
		}
	}
	ArtifactEmitter::Emit(std::move(Model), OutputDirectory);
}
} // namespace Mod::HeaderTool
