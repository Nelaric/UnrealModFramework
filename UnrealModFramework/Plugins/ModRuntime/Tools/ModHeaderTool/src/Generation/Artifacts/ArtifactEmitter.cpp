// Copyright (c) 2026 Nelaric Contributors
#include "ArtifactEmitter.h"

#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Generation/Artifacts/ArtifactRenderer.h"
#include "ModHeaderTool/src/Generation/Artifacts/GeneratedBatch.h"
#include "ModHeaderTool/src/Generation/Artifacts/GenerationDestination.h"
#include "ModHeaderTool/src/Generation/Artifacts/GenerationManifest.h"
#include "ModHeaderTool/src/Generation/Artifacts/GenerationOutput.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

void ArtifactEmitter::Emit(GenerationModel Model, const fs::path& OutputDirectory)
{
	auto Batch = ArtifactRenderer::RenderArtifacts(Model);
	const auto Manifest = GenerationManifest::MakeManifest(Model, Batch);
	const std::string ManifestName = Model.Input.GameId + ".generated-artifacts.json";
	const std::string ManifestBytes = Manifest.dump(2) + '\n';
	const auto Root = GenerationDestination::ResolveOutputRoot(Model, OutputDirectory);
	Batch.Files[ManifestName] = ManifestBytes;
	GenerationOutput::PublishGeneratedFiles(Root, Batch.Files, ManifestName);
	std::cout << Path::PathToUtf8(Root / ManifestName) << '\n';
}
} // namespace Mod::HeaderTool
